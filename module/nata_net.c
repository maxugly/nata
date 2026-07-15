#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "nata.h"

/*
 * Wake the producer netdev after freeing an RX slot.
 * is_dev0: this NAPI is nata0 (drains nata1 TX) → wake nata1.
 */
static void nata_wake_tx_peer(struct nata_priv *priv, int is_dev0)
{
	struct net_device *txdev = is_dev0 ? priv->netdev1 : priv->netdev0;

	if (txdev && netif_queue_stopped(txdev))
		netif_wake_queue(txdev);
}

/*
 * NAPI poll for either nata0 or nata1.
 * Ring dequeue under short spinlock; stack inject outside the lock so TX
 * can keep filling slots while we hand skbs to the peer stack.
 */
static int nata_poll(struct napi_struct *napi, int budget)
{
	struct net_device *dev = napi->dev;
	struct nata_priv *priv = *(struct nata_priv **)netdev_priv(dev);
	int is_dev0 = (dev == priv->netdev0) ? 1 : 0;
	int work = 0;

	while (work < budget) {
		struct sk_buff *skb = NULL;
		int ret;

		if (!check_rx_pending(priv, is_dev0))
			break;

		spin_lock(&priv->lock);
		ret = sim_rx_dequeue(priv, is_dev0, &skb);
		if (ret != 0)
			nata_wake_tx_peer(priv, is_dev0);
		spin_unlock(&priv->lock);

		if (ret == 0)
			break;
		if (ret < 0)
			continue; /* poison/oom slot already consumed */

		skb->protocol = eth_type_trans(skb, dev);
		napi_gro_receive(napi, skb);
		work++;
	}

	if (work < budget) {
		napi_complete_done(napi, work);
		/*
		 * Race: peer may publish after we saw empty but before
		 * complete. Reschedule if the tail slot is live.
		 */
		if (check_rx_pending(priv, is_dev0))
			napi_schedule(napi);
	}

	return work;
}

/*
 * Common TX: enqueue, apply queue stop/wake backpressure, schedule peer NAPI.
 * is_dev0: 1 = nata0, 0 = nata1.
 */
static netdev_tx_t nata_xmit(struct sk_buff *skb, struct net_device *dev,
			     int is_dev0)
{
	struct nata_priv *priv = *(struct nata_priv **)netdev_priv(dev);
	struct napi_struct *peer_napi;
	int ret;

	if (!priv || !priv->sim_mailbox) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	peer_napi = is_dev0 ? &priv->napi1 : &priv->napi0;

	spin_lock_bh(&priv->lock);

	ret = sim_tx_packet(priv, skb, is_dev0);
	if (ret == -ENOSPC) {
		/*
		 * Ring full: stop this TX queue and ask the stack to retry.
		 * Do not free the skb; do not count as a packet drop.
		 */
		priv->ring_full_drops++;
		netif_stop_queue(dev);
		spin_unlock_bh(&priv->lock);
		/* Ensure peer is draining; full ring means consumer is behind */
		napi_schedule(peer_napi);
		return NETDEV_TX_BUSY;
	}

	if (ret == 0) {
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb->len;
		if (is_dev0) {
			priv->tx_packets_0++;
			priv->tx_bytes_0 += skb->len;
		} else {
			priv->tx_packets_1++;
			priv->tx_bytes_1 += skb->len;
		}
		/* Proactive stop if this fill exhausted free slots */
		if (check_tx_full(priv, is_dev0))
			netif_stop_queue(dev);
	} else {
		dev->stats.tx_errors++;
	}

	spin_unlock_bh(&priv->lock);

	if (ret == 0)
		napi_schedule(peer_napi);

	if (ret == 0)
		dev_consume_skb_any(skb);
	else
		dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static netdev_tx_t nata_xmit_0(struct sk_buff *skb, struct net_device *dev)
{
	return nata_xmit(skb, dev, 1);
}

static netdev_tx_t nata_xmit_1(struct sk_buff *skb, struct net_device *dev)
{
	return nata_xmit(skb, dev, 0);
}

static int nata_open(struct net_device *dev)
{
	struct nata_priv *priv = *(struct nata_priv **)netdev_priv(dev);

	if (dev == priv->netdev0)
		napi_enable(&priv->napi0);
	else
		napi_enable(&priv->napi1);

	netif_start_queue(dev);
	return 0;
}

static int nata_stop(struct net_device *dev)
{
	struct nata_priv *priv = *(struct nata_priv **)netdev_priv(dev);

	netif_stop_queue(dev);

	if (dev == priv->netdev0)
		napi_disable(&priv->napi0);
	else
		napi_disable(&priv->napi1);

	return 0;
}

static const struct net_device_ops nata_netdev_ops_0 = {
	.ndo_open = nata_open,
	.ndo_stop = nata_stop,
	.ndo_start_xmit = nata_xmit_0,
};

static const struct net_device_ops nata_netdev_ops_1 = {
	.ndo_open = nata_open,
	.ndo_stop = nata_stop,
	.ndo_start_xmit = nata_xmit_1,
};

/* Initialization routine called from nata_main.c */
int nata_net_init(struct nata_priv *priv)
{
	struct net_device *dev0, *dev1;
	struct nata_priv **priv_ptr0, **priv_ptr1;

	/* Geometry must stay 128 KiB total (two 64 KiB halves) */
	BUILD_BUG_ON(NATA_MAILBOX_BYTES != 131072);
	BUILD_BUG_ON(NATA_RING_SLOTS * NATA_SLOT_BYTES != 65536);
	BUILD_BUG_ON(NATA_SLOT_BYTES - NATA_SLOT_PAYLOAD_OFF < ETH_FRAME_LEN);

	/* 1. Register Virtual NIC nata0 */
	dev0 = alloc_etherdev(sizeof(struct nata_priv *));
	if (!dev0)
		return -ENOMEM;

	snprintf(dev0->name, IFNAMSIZ, "nata0");
	dev0->netdev_ops = &nata_netdev_ops_0;
	eth_hw_addr_random(dev0);

	priv_ptr0 = netdev_priv(dev0);
	*priv_ptr0 = priv;
	priv->netdev0 = dev0;

	netif_napi_add(dev0, &priv->napi0, nata_poll);

	if (register_netdev(dev0)) {
		pr_err("NATA: Failed to register virtual interface nata0.\n");
		netif_napi_del(&priv->napi0);
		free_netdev(dev0);
		priv->netdev0 = NULL;
		return -ENODEV;
	}

	/* 2. Register Virtual NIC nata1 */
	dev1 = alloc_etherdev(sizeof(struct nata_priv *));
	if (!dev1) {
		unregister_netdev(dev0);
		netif_napi_del(&priv->napi0);
		free_netdev(dev0);
		priv->netdev0 = NULL;
		return -ENOMEM;
	}

	snprintf(dev1->name, IFNAMSIZ, "nata1");
	dev1->netdev_ops = &nata_netdev_ops_1;
	eth_hw_addr_random(dev1);

	priv_ptr1 = netdev_priv(dev1);
	*priv_ptr1 = priv;
	priv->netdev1 = dev1;

	netif_napi_add(dev1, &priv->napi1, nata_poll);

	if (register_netdev(dev1)) {
		pr_err("NATA: Failed to register virtual interface nata1.\n");
		netif_napi_del(&priv->napi1);
		free_netdev(dev1);
		priv->netdev1 = NULL;
		unregister_netdev(dev0);
		netif_napi_del(&priv->napi0);
		free_netdev(dev0);
		priv->netdev0 = NULL;
		return -ENODEV;
	}

	/* Bring carrier up on both virtual devices */
	netif_carrier_on(dev0);
	netif_carrier_on(dev1);

	pr_info("NATA: Registered nata0/nata1 (NAPI RX, %d-slot rings, TX backpressure).\n",
		NATA_RING_SLOTS);
	return 0;
}
