#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
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
    size_t total_len;
    size_t sectors_needed;
    u64 sector = is_dev0 ? priv->tx_lba_0 : priv->tx_lba_1;
    u64 offset = sector * 512;
    u32 *tx_seq = is_dev0 ? &priv->tx_seq_0 : &priv->tx_seq_1;

    if (!priv->sim_mailbox)
        return -ENODEV;

    /* Build the packet header */
    hdr.magic = NATA_MAGIC;
    hdr.len = skb->len;
    hdr.seq = ++(*tx_seq);
    hdr.reserved = 0;

    total_len = sizeof(hdr) + skb->len;
    sectors_needed = (total_len + 511) / 512;

    /* Ensure we do not access out-of-bounds memory (128 KB limit) */
    if (offset + sectors_needed * 512 > 131072)
        return -EINVAL;

    /* Write directly to simulated memory space */
    memcpy(priv->sim_mailbox + offset, &hdr, sizeof(hdr));
    memcpy(priv->sim_mailbox + offset + sizeof(hdr), skb->data, skb->len);

    return 0;
}

/* * Simulated receive function.
 * Reads the packet from simulated memory, unpacks it, and injects it into the network stack.
 */
int sim_rx_one_packet(struct nata_priv *priv, int is_dev0)
{
    struct nata_pkt_hdr hdr;
    struct sk_buff *skb;
    u64 sector = is_dev0 ? priv->rx_lba_0 : priv->rx_lba_1;
    u64 offset = sector * 512;
    struct net_device *netdev = is_dev0 ? priv->netdev0 : priv->netdev1;
    u32 *last_rx_seq = is_dev0 ? &priv->last_rx_seq_0 : &priv->last_rx_seq_1;
    u64 *rx_packets = is_dev0 ? &priv->rx_packets_0 : &priv->rx_packets_1;
    u64 *rx_bytes = is_dev0 ? &priv->rx_bytes_0 : &priv->rx_bytes_1;
    size_t total_len;
    size_t sectors_needed;

    if (!priv->sim_mailbox)
        return -ENODEV;

    /* Ensure we can at least read the header safely */
    if (offset + sizeof(hdr) > 131072)
        return -EINVAL;

    /* Read header directly from simulated BRAM */
    memcpy(&hdr, priv->sim_mailbox + offset, sizeof(hdr));

    if (hdr.magic != NATA_MAGIC || hdr.seq == *last_rx_seq) {
        return 0; /* No new packet */
    }

    if (hdr.len > ETH_FRAME_LEN || hdr.len < ETH_HLEN) {
        priv->dropped_blocks++;
        *last_rx_seq = hdr.seq;
        return -EINVAL;
    }

    total_len = sizeof(hdr) + hdr.len;
    sectors_needed = (total_len + 511) / 512;

    if (offset + sectors_needed * 512 > 131072) {
        priv->dropped_blocks++;
        *last_rx_seq = hdr.seq;
        return -EINVAL;
    }

    /* Allocate skb for receiving */
    skb = dev_alloc_skb(hdr.len + 2);
    if (!skb) {
        priv->dropped_blocks++;
        return -ENOMEM;
    }
    skb_reserve(skb, 2); /* Align IP header on 16-byte boundary */

    /* Copy packet data directly from simulated memory space */
    memcpy(skb_put(skb, hdr.len), priv->sim_mailbox + offset + sizeof(hdr), hdr.len);

    /* Inject packet into network stack */
    skb->dev = netdev;
    skb->protocol = eth_type_trans(skb, netdev);
    netif_rx(skb);

    *rx_packets += 1;
    *rx_bytes += hdr.len;
    *last_rx_seq = hdr.seq;

    return 1;
}
