#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "nata.h"

/* * Memory-based simulated block I/O helper.
 * Simulates writing/reading sectors to the FPGA SRAM dual-port RAM mailbox.
 */
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf, size_t len, int op)
{
    u64 offset = sector * 512;

    if (!priv->sim_mailbox)
        return -ENODEV;

    /* Ensure we do not access out-of-bounds memory (128 KB limit) */
    if (offset + len > 131072)
        return -EINVAL;

    if (op == 1) {
        /* Write to simulated BRAM */
        memcpy(priv->sim_mailbox + offset, buf, len);
    } else {
        /* Read from simulated BRAM */
        memcpy(buf, priv->sim_mailbox + offset, len);
    }
    return 0;
}

/* * Check if there is a new packet waiting to be received.
 * Returns 1 if a packet with a new sequence number is present, 0 otherwise.
 */
int check_rx_pending(struct nata_priv *priv, int is_dev0)
{
    u64 offset = (is_dev0 ? priv->rx_lba_0 : priv->rx_lba_1) * 512;
    struct nata_pkt_hdr *hdr;

    if (!priv->sim_mailbox)
        return 0;

    hdr = (struct nata_pkt_hdr *)(priv->sim_mailbox + offset);
    return (hdr->magic == NATA_MAGIC && 
            hdr->seq != (is_dev0 ? priv->last_rx_seq_0 : priv->last_rx_seq_1));
}

/* * Simulated transmit function.
 * Packs the network frame into a NATA sector packet and writes it to memory.
 */
int sim_tx_packet(struct nata_priv *priv, struct sk_buff *skb, int is_dev0)
{
    struct nata_pkt_hdr hdr;
    void *buf;
    size_t total_len;
    size_t sectors_needed;
    u64 sector = is_dev0 ? priv->tx_lba_0 : priv->tx_lba_1;
    u32 *tx_seq = is_dev0 ? &priv->tx_seq_0 : &priv->tx_seq_1;
    int err;

    if (!priv->sim_mailbox)
        return -ENODEV;

    /* Build the packet header */
    hdr.magic = NATA_MAGIC;
    hdr.len = skb->len;
    hdr.seq = ++(*tx_seq);
    hdr.reserved = 0;

    total_len = sizeof(hdr) + skb->len;
    sectors_needed = (total_len + 511) / 512;

    /* Use GFP_ATOMIC since this is called from the netdev start_xmit atomic context */
    buf = kzalloc(sectors_needed * 512, GFP_ATOMIC);
    if (!buf)
        return -ENOMEM;

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), skb->data, skb->len);

    /* Write packet directly to simulated memory space */
    err = sim_mailbox_io(priv, sector, buf, sectors_needed * 512, 1);

    kfree(buf);
    return err;
}

/* * Simulated receive function.
 * Reads the packet from simulated memory, unpacks it, and injects it into the network stack.
 */
int sim_rx_one_packet(struct nata_priv *priv, int is_dev0)
{
    struct nata_pkt_hdr hdr;
    struct sk_buff *skb;
    void *buf;
    int err;
    u64 sector = is_dev0 ? priv->rx_lba_0 : priv->rx_lba_1;
    struct net_device *netdev = is_dev0 ? priv->netdev0 : priv->netdev1;
    u32 *last_rx_seq = is_dev0 ? &priv->last_rx_seq_0 : &priv->last_rx_seq_1;
    u64 *rx_packets = is_dev0 ? &priv->rx_packets_0 : &priv->rx_packets_1;
    u64 *rx_bytes = is_dev0 ? &priv->rx_bytes_0 : &priv->rx_bytes_1;

    buf = kzalloc(512, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    err = sim_mailbox_io(priv, sector, buf, 512, 0);
    if (err) {
        kfree(buf);
        return err;
    }

    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != NATA_MAGIC || hdr.seq == *last_rx_seq) {
        kfree(buf);
        return 0; /* No new packet */
    }

    if (hdr.len > ETH_FRAME_LEN || hdr.len < ETH_HLEN) {
        priv->dropped_blocks++;
        kfree(buf);
        return -EINVAL;
    }

    /* Allocate skb for receiving */
    skb = dev_alloc_skb(hdr.len + 2);
    if (!skb) {
        priv->dropped_blocks++;
        kfree(buf);
        return -ENOMEM;
    }
    skb_reserve(skb, 2); /* Align IP header on 16-byte boundary */

    if (hdr.len <= 496) {
        /* Entire packet fits in the first sector */
        memcpy(skb_put(skb, hdr.len), buf + sizeof(hdr), hdr.len);
    } else {
        /* Read remaining sectors */
        size_t total_len = sizeof(hdr) + hdr.len;
        size_t sectors_needed = (total_len + 511) / 512;
        void *large_buf = kzalloc(sectors_needed * 512, GFP_KERNEL);
        if (!large_buf) {
            dev_kfree_skb(skb);
            kfree(buf);
            return -ENOMEM;
        }

        err = sim_mailbox_io(priv, sector, large_buf, sectors_needed * 512, 0);
        if (err) {
            dev_kfree_skb(skb);
            kfree(large_buf);
            kfree(buf);
            return err;
        }

        memcpy(skb_put(skb, hdr.len), large_buf + sizeof(hdr), hdr.len);
        kfree(large_buf);
    }

    kfree(buf);

    /* Inject packet into network stack */
    skb->dev = netdev;
    skb->protocol = eth_type_trans(skb, netdev);
    netif_rx(skb);

    *rx_packets += 1;
    *rx_bytes += hdr.len;
    *last_rx_seq = hdr.seq;

    return 1;
}
