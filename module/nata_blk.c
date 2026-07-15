#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <asm/barrier.h>
#include "nata.h"

/* 128 KB simulated dual-port mailbox = 256 sectors of 512 bytes */
#define NATA_MAILBOX_BYTES  131072
#define NATA_MAILBOX_SECTORS    (NATA_MAILBOX_BYTES / 512)

/*
 * Memory-based simulated block I/O helper.
 * Simulates writing/reading sectors to the FPGA SRAM dual-port RAM mailbox.
 */
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf, size_t len, int op)
{
    u64 offset;

    if (!priv->sim_mailbox)
        return -ENODEV;

    /* Sector-based check avoids overflow in sector * 512 */
    if (sector >= NATA_MAILBOX_SECTORS || len > NATA_MAILBOX_BYTES)
        return -EINVAL;

    offset = sector * 512;
    if (len > NATA_MAILBOX_BYTES - offset)
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

/*
 * Check if there is a new packet waiting to be received.
 * Returns 1 if a packet with a new sequence number is present, 0 otherwise.
 */
int check_rx_pending(struct nata_priv *priv, int is_dev0)
{
    u64 sector = is_dev0 ? priv->rx_lba_0 : priv->rx_lba_1;
    struct nata_pkt_hdr *hdr;

    if (!priv->sim_mailbox || sector >= NATA_MAILBOX_SECTORS)
        return 0;

    hdr = (struct nata_pkt_hdr *)(priv->sim_mailbox + sector * 512);
    return (hdr->magic == NATA_MAGIC &&
        hdr->seq != (is_dev0 ? priv->last_rx_seq_0 : priv->last_rx_seq_1));
}

/*
 * Simulated transmit function.
 * Packs the network frame into a NATA sector packet and writes it to memory.
 *
 * Publication order: payload first, then smp_wmb(), then header. Readers that
 * observe a new seq/magic are guaranteed to see the matching payload.
 * Caller must hold priv->lock (taken in the xmit path).
 */
int sim_tx_packet(struct nata_priv *priv, struct sk_buff *skb, int is_dev0)
{
    struct nata_pkt_hdr hdr;
    size_t total_len;
    size_t sectors_needed;
    u64 sector = is_dev0 ? priv->tx_lba_0 : priv->tx_lba_1;
    u64 offset;
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

    /* Validate in sector units to avoid integer overflow on offset math */
    if (sector >= NATA_MAILBOX_SECTORS ||
        sectors_needed > NATA_MAILBOX_SECTORS ||
        sector + sectors_needed > NATA_MAILBOX_SECTORS)
        return -EINVAL;

    offset = sector * 512;

    /* Write payload first, barrier, then publish via header */
    memcpy(priv->sim_mailbox + offset + sizeof(hdr), skb->data, skb->len);
    smp_wmb();
    memcpy(priv->sim_mailbox + offset, &hdr, sizeof(hdr));

    return 0;
}

/*
 * Simulated receive function.
 * Reads the packet from simulated memory, unpacks it, and injects it into the network stack.
 *
 * On any dropped/invalid entry, last_rx_seq is advanced so the RX thread does not
 * spin forever on a bad mailbox slot.
 */
int sim_rx_one_packet(struct nata_priv *priv, int is_dev0)
{
    struct nata_pkt_hdr hdr;
    struct sk_buff *skb;
    u64 sector = is_dev0 ? priv->rx_lba_0 : priv->rx_lba_1;
    u64 offset;
    struct net_device *netdev = is_dev0 ? priv->netdev0 : priv->netdev1;
    u32 *last_rx_seq = is_dev0 ? &priv->last_rx_seq_0 : &priv->last_rx_seq_1;
    u64 *rx_packets = is_dev0 ? &priv->rx_packets_0 : &priv->rx_packets_1;
    u64 *rx_bytes = is_dev0 ? &priv->rx_bytes_0 : &priv->rx_bytes_1;
    size_t total_len;
    size_t sectors_needed;

    if (!priv->sim_mailbox)
        return -ENODEV;

    if (sector >= NATA_MAILBOX_SECTORS)
        return -EINVAL;

    offset = sector * 512;

    /* Read header directly from simulated BRAM */
    memcpy(&hdr, priv->sim_mailbox + offset, sizeof(hdr));

    if (hdr.magic != NATA_MAGIC || hdr.seq == *last_rx_seq)
        return 0; /* No new packet */

    /*
     * eth_type_trans() pulls ETH_HLEN bytes; frames smaller than that panic.
     * Consume the sequence so a bad entry cannot busy-loop the RX thread.
     */
    if (hdr.len > ETH_FRAME_LEN || hdr.len < ETH_HLEN) {
        priv->dropped_blocks++;
        *last_rx_seq = hdr.seq;
        return -EINVAL;
    }

    total_len = sizeof(hdr) + hdr.len;
    sectors_needed = (total_len + 511) / 512;

    if (sectors_needed > NATA_MAILBOX_SECTORS ||
        sector + sectors_needed > NATA_MAILBOX_SECTORS) {
        priv->dropped_blocks++;
        *last_rx_seq = hdr.seq;
        return -EINVAL;
    }

    /* Allocate skb for receiving */
    skb = dev_alloc_skb(hdr.len + 2);
    if (!skb) {
        priv->dropped_blocks++;
        *last_rx_seq = hdr.seq;
        return -ENOMEM;
    }
    skb_reserve(skb, 2); /* Align IP header on 16-byte boundary */

    /* Pair with TX smp_wmb(): payload is visible after a valid new header */
    smp_rmb();

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
