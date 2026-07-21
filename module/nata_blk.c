#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <asm/barrier.h>
#include "nata.h"

/* 128 KB simulated dual-port mailbox = 256 sectors of 512 bytes */
#define NATA_MAILBOX_SECTORS	(NATA_MAILBOX_BYTES / 512)

/* Max usable payload in a slot after valid + header */
#define NATA_SLOT_MAX_PAYLOAD	(NATA_SLOT_BYTES - NATA_SLOT_PAYLOAD_OFF)

/*
 * Byte offset of slot index within a half starting at base_lba.
 * base_lba is 0 (lower) or 128 (upper); slot is 0 .. NATA_RING_SLOTS-1.
 */
static inline u64 nata_slot_byte_offset(u64 base_lba, u32 slot)
{
	return (base_lba + (u64)(slot & NATA_RING_MASK) * NATA_SLOT_SECTORS) * 512;
}

static inline u8 *nata_slot_ptr(struct nata_priv *priv, u64 base_lba, u32 slot)
{
	return priv->sim_mailbox + nata_slot_byte_offset(base_lba, slot);
}

static inline u32 nata_slot_read_valid(u8 *slot)
{
	u32 v;

	memcpy(&v, slot + NATA_SLOT_VALID_OFF, sizeof(v));
	return v;
}

static inline void nata_slot_write_valid(u8 *slot, u32 v)
{
	memcpy(slot + NATA_SLOT_VALID_OFF, &v, sizeof(v));
}

/*
 * Memory-based simulated block I/O helper.
 * Sector-oriented; ring hot path uses nata_slot_ptr directly.
 */
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf, size_t len, int op)
{
	u64 offset;

	if (!priv->sim_mailbox)
		return -ENODEV;

	if (sector >= NATA_MAILBOX_SECTORS || len > NATA_MAILBOX_BYTES)
		return -EINVAL;

	offset = sector * 512;
	if (len > NATA_MAILBOX_BYTES - offset)
		return -EINVAL;

	if (op == 1)
		memcpy(priv->sim_mailbox + offset, buf, len);
	else
		memcpy(buf, priv->sim_mailbox + offset, len);
	return 0;
}

/*
 * True if the RX ring for this netdev has at least one published slot.
 * is_dev0: nata0 consumes lower half (nata1 TX ring: head/tail _1).
 * Peek may run without priv->lock (wake / NAPI reschedule decision only).
 */
int check_rx_pending(struct nata_priv *priv, int is_dev0)
{
	u64 base_lba;
	u32 tail;
	u8 *slot;

	if (!priv->sim_mailbox)
		return 0;

	if (is_dev0) {
		base_lba = priv->rx_lba_0;
		tail = priv->tx_tail_1;
	} else {
		base_lba = priv->rx_lba_1;
		tail = priv->tx_tail_0;
	}

	slot = nata_slot_ptr(priv, base_lba, tail);
	return nata_slot_read_valid(slot) == 1;
}

/*
 * True if the TX ring for this netdev cannot accept another frame
 * (slot at head still published). Caller should hold priv->lock for a
 * stable decision with enqueue/stop-queue.
 */
int check_tx_full(struct nata_priv *priv, int is_dev0)
{
	u64 base_lba;
	u32 head;
	u8 *slot;

	if (!priv->sim_mailbox)
		return 1;

	if (is_dev0) {
		base_lba = priv->tx_lba_0;
		head = priv->tx_head_0;
	} else {
		base_lba = priv->tx_lba_1;
		head = priv->tx_head_1;
	}

	slot = nata_slot_ptr(priv, base_lba, head);
	return nata_slot_read_valid(slot) == 1;
}

/*
 * Enqueue one frame into the peer-facing ring (producer).
 * Full ring: no overwrite — return -ENOSPC (caller applies NETDEV_TX_BUSY).
 *
 * Order: payload + header, smp_wmb(), then valid=1; advance head.
 * Caller must hold priv->lock.
 */
int sim_tx_packet(struct nata_priv *priv, struct sk_buff *skb, int is_dev0)
{
	struct nata_pkt_hdr hdr;
	u64 base_lba;
	u32 *head;
	u32 *tx_seq;
	u32 h;
	u8 *slot;
	size_t max_payload;

	if (!priv->sim_mailbox)
		return -ENODEV;

	if (is_dev0) {
		base_lba = priv->tx_lba_0;
		head = &priv->tx_head_0;
		tx_seq = &priv->tx_seq_0;
	} else {
		base_lba = priv->tx_lba_1;
		head = &priv->tx_head_1;
		tx_seq = &priv->tx_seq_1;
	}

	h = *head & NATA_RING_MASK;
	slot = nata_slot_ptr(priv, base_lba, h);

	/* Full: head slot still published (consumer has not cleared it) */
	if (nata_slot_read_valid(slot) == 1)
		return -ENOSPC;

	max_payload = NATA_SLOT_MAX_PAYLOAD;
	if (skb->len < ETH_HLEN || skb->len > ETH_FRAME_LEN ||
	    skb->len > max_payload)
		return -EINVAL;

	hdr.magic = NATA_MAGIC;
	hdr.len = skb->len;
	hdr.seq = ++(*tx_seq);
	hdr.reserved = 0;

	/* Ensure clean valid before filling (stale clear) */
	nata_slot_write_valid(slot, 0);

	memcpy(slot + NATA_SLOT_PAYLOAD_OFF, skb->data, skb->len);
	memcpy(slot + NATA_SLOT_HDR_OFF, &hdr, sizeof(hdr));
	smp_wmb();
	nata_slot_write_valid(slot, 1);

	*head = (*head + 1) & NATA_RING_MASK;
	return 0;
}

/*
 * Dequeue one frame from this netdev's RX ring into *skbp.
 * On any drop/invalid, clear valid and advance tail so RX cannot busy-loop.
 * Caller must hold priv->lock. Does not call into the stack (no inject).
 *
 * Returns: 1 and *skbp set, 0 empty, <0 after consume-drop (no skb).
 */
int sim_rx_dequeue(struct nata_priv *priv, int is_dev0, struct sk_buff **skbp)
{
	struct nata_pkt_hdr hdr;
	struct sk_buff *skb;
	struct net_device *netdev;
	struct napi_struct *napi;
	u64 base_lba;
	u32 *tail;
	u64 *rx_packets;
	u64 *rx_bytes;
	u32 t;
	u8 *slot;

	*skbp = NULL;

	if (!priv->sim_mailbox)
		return -ENODEV;

	if (is_dev0) {
		/* nata0 RX ← lower half (nata1 TX ring) */
		base_lba = priv->rx_lba_0;
		tail = &priv->tx_tail_1;
		netdev = priv->netdev0;
		napi = &priv->napi0;
		rx_packets = &priv->rx_packets_0;
		rx_bytes = &priv->rx_bytes_0;
	} else {
		/* nata1 RX ← upper half (nata0 TX ring) */
		base_lba = priv->rx_lba_1;
		tail = &priv->tx_tail_0;
		netdev = priv->netdev1;
		napi = &priv->napi1;
		rx_packets = &priv->rx_packets_1;
		rx_bytes = &priv->rx_bytes_1;
	}

	t = *tail & NATA_RING_MASK;
	slot = nata_slot_ptr(priv, base_lba, t);

	if (nata_slot_read_valid(slot) != 1)
		return 0; /* empty */

	/* Payload/header visible after observing valid==1 */
	smp_rmb();
	memcpy(&hdr, slot + NATA_SLOT_HDR_OFF, sizeof(hdr));

	/*
	 * Consume slot on every path after valid observed so a poison entry
	 * cannot pin the NAPI poller.
	 */
	if (hdr.magic != NATA_MAGIC ||
	    hdr.len > ETH_FRAME_LEN || hdr.len < ETH_HLEN ||
	    hdr.len > NATA_SLOT_MAX_PAYLOAD) {
		priv->dropped_blocks++;
		nata_slot_write_valid(slot, 0);
		*tail = (*tail + 1) & NATA_RING_MASK;
		return -EINVAL;
	}

	/* Use NAPI allocator for per-CPU cache performance in polling context */
	skb = napi_alloc_skb(napi, hdr.len + 2);
	if (!skb) {
		priv->dropped_blocks++;
		nata_slot_write_valid(slot, 0);
		*tail = (*tail + 1) & NATA_RING_MASK;
		return -ENOMEM;
	}
	skb_reserve(skb, 2);

	memcpy(skb_put(skb, hdr.len), slot + NATA_SLOT_PAYLOAD_OFF, hdr.len);

	/* Free the slot before inject so the producer can reuse it promptly */
	nata_slot_write_valid(slot, 0);
	*tail = (*tail + 1) & NATA_RING_MASK;

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += hdr.len;
	*rx_packets += 1;
	*rx_bytes += hdr.len;

	skb->dev = netdev;
	*skbp = skb;
	return 1;
}
