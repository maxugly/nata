#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "nata.h"

/* * Transmit function for nata0.
 * Writes the packet to simulated mailbox LBA 0 and wakes up nata1 RX thread.
 */
static netdev_tx_t nata_xmit_0(struct sk_buff *skb, struct net_device *dev)
{
    struct nata_priv *priv = *(struct nata_priv **)netdev_priv(dev);
    int ret;

    if (!priv || !priv->sim_mailbox) {
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    spin_lock_bh(&priv->lock);

    ret = sim_tx_packet(priv, skb, 1); /* 1 for dev0 (nata0) */
    if (ret == 0) {
        dev->stats.tx_packets++;
        dev->stats.tx_bytes += skb->len;
        priv->tx_packets_0++;
        priv->tx_bytes_0 += skb->len;

        /* Wake up peer interface (nata1) RX polling thread */
        wake_up_interruptible(&priv->rx_wait_1);
    } else {
        dev->stats.tx_errors++;
    }

    /* Drop the skb outside the lock to shorten hold time */
    spin_unlock_bh(&priv->lock);
    dev_consume_skb_any(skb);
    return NETDEV_TX_OK;
}

/* * Transmit function for nata1.
 * Writes the packet to simulated mailbox LBA 100 and wakes up nata0 RX thread.
 */
static netdev_tx_t nata_xmit_1(struct sk_buff *skb, struct net_device *dev)
{
    struct nata_priv *priv = *(struct nata_priv **)netdev_priv(dev);
    int ret;

    if (!priv || !priv->sim_mailbox) {
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    spin_lock_bh(&priv->lock);

    ret = sim_tx_packet(priv, skb, 0); /* 0 for dev1 (nata1) */
    if (ret == 0) {
        dev->stats.tx_packets++;
        dev->stats.tx_bytes += skb->len;
        priv->tx_packets_1++;
        priv->tx_bytes_1 += skb->len;

        /* Wake up peer interface (nata0) RX polling thread */
        wake_up_interruptible(&priv->rx_wait_0);
    } else {
        dev->stats.tx_errors++;
    }

    /* Drop the skb outside the lock to shorten hold time */
    spin_unlock_bh(&priv->lock);
    dev_consume_skb_any(skb);
    return NETDEV_TX_OK;
}

static int nata_open(struct net_device *dev)
{
    netif_start_queue(dev);
    return 0;
}

static int nata_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    return 0;
}

/* Operations structure for virtual interface nata0 */
static const struct net_device_ops nata_netdev_ops_0 = {
    .ndo_open = nata_open,
    .ndo_stop = nata_stop,
    .ndo_start_xmit = nata_xmit_0,
};

/* Operations structure for virtual interface nata1 */
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

    if (register_netdev(dev0)) {
        pr_err("NATA: Failed to register virtual interface nata0.\n");
        free_netdev(dev0);
        priv->netdev0 = NULL;
        return -ENODEV;
    }

    /* 2. Register Virtual NIC nata1 */
    dev1 = alloc_etherdev(sizeof(struct nata_priv *));
    if (!dev1) {
        unregister_netdev(dev0);
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

    if (register_netdev(dev1)) {
        pr_err("NATA: Failed to register virtual interface nata1.\n");
        free_netdev(dev1);
        priv->netdev1 = NULL;
        unregister_netdev(dev0);
        free_netdev(dev0);
        priv->netdev0 = NULL;
        return -ENODEV;
    }

    /* Bring carrier up on both virtual devices */
    netif_carrier_on(dev0);
    netif_carrier_on(dev1);

    pr_info("NATA: Registered virtual interfaces nata0 and nata1 successfully.\n");
    return 0;
}