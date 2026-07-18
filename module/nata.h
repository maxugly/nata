#ifndef _NATA_H_
#define _NATA_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#else
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define NATA_MAGIC 0x4E415441  /* "NATA" in ASCII */

/*
 * Per-direction ring: 32 slots × 4 sectors × 512 B = 64 KiB (one mailbox half).
 * Depth 32 absorbs TCP bursts better than the old 8×16 layout; slot still fits
 * ETH_FRAME_LEN (1514) + valid + header (payload max 2028).
 */
#define NATA_RING_SLOTS		32	/* power of 2 */
#define NATA_SLOT_SECTORS	4	/* 2048 bytes per slot */
#define NATA_SLOT_BYTES		(NATA_SLOT_SECTORS * 512)
#define NATA_RING_MASK		(NATA_RING_SLOTS - 1)
#define NATA_MAILBOX_HALF_BYTES	(NATA_RING_SLOTS * NATA_SLOT_BYTES)	/* 65536 */
#define NATA_MAILBOX_BYTES	(NATA_MAILBOX_HALF_BYTES * 2)		/* 131072 */

struct nata_pkt_hdr {
	u32 magic;
	u32 len;
	u32 seq;
	u32 reserved;
};

/* On-mailbox slot layout (within each NATA_SLOT_BYTES slot) */
#define NATA_SLOT_VALID_OFF	0	/* u32: 0 = empty, 1 = published */
#define NATA_SLOT_HDR_OFF	4	/* struct nata_pkt_hdr */
#define NATA_SLOT_PAYLOAD_OFF	(NATA_SLOT_HDR_OFF + (int)sizeof(struct nata_pkt_hdr))

#ifdef __KERNEL__
struct nata_priv {
	struct net_device *netdev0;
	struct net_device *netdev1;
	spinlock_t lock;

	/* Shared Memory Mailbox for Simulation Mode (128KB allocated via vmalloc) */
	u8 *sim_mailbox;

	/* LBA bases for each half (slot 0 starts here; ring uses + i * NATA_SLOT_SECTORS)
	 * Lower 64KB is LBA 0 to 127 (offset 0)   — nata1 TX / nata0 RX
	 * Upper 64KB is LBA 128 to 255 (offset 65536) — nata0 TX / nata1 RX
	 */
	u64 tx_lba_0; /* 128 (upper 64KB) */
	u64 rx_lba_0; /* 0   (lower 64KB) */
	u64 tx_lba_1; /* 0   (lower 64KB) */
	u64 rx_lba_1; /* 128 (upper 64KB) */

	/* Per-netdev NAPI (softirq RX drain; replaces kthreads) */
	struct napi_struct napi0;
	struct napi_struct napi1;

	/*
	 * Ring indices (0 .. NATA_RING_SLOTS-1) per direction.
	 * head_0/tail_0: upper half (nata0 TX / nata1 RX).
	 * head_1/tail_1: lower half (nata1 TX / nata0 RX).
	 * Producer advances head; consumer advances tail.
	 * Full: slot at head has valid==1. Empty: slot at tail has valid==0.
	 */
	u32 tx_head_0;
	u32 tx_tail_0;
	u32 tx_head_1;
	u32 tx_tail_1;

	/* Optional observability sequence written into nata_pkt_hdr.seq */
	u32 tx_seq_0;
	u32 tx_seq_1;

	/* Stats for nata0 */
	u64 tx_packets_0;
	u64 tx_bytes_0;
	u64 rx_packets_0;
	u64 rx_bytes_0;

	/* Stats for nata1 */
	u64 tx_packets_1;
	u64 tx_bytes_1;
	u64 rx_packets_1;
	u64 rx_bytes_1;

	u64 dropped_blocks;	/* invalid frames + inject fails (not TX busy) */
	u64 ring_full_drops;	/* TX saw full ring → NETDEV_TX_BUSY (not a drop) */
};
#endif /* __KERNEL__ */

struct nata_ioc_bind {
	char bdev_path[128];
	u64 tx_lba_start;
	u64 rx_lba_start;
};

struct nata_ioc_status {
	char bdev_path[128];
	u64 tx_lba_start;
	u64 rx_lba_start;
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_packets;
	u64 rx_bytes;
	u64 dropped_blocks;
	u64 interrupt_counts;
	int is_bound;
	int is_sim_mode;

	/* Stats for both interfaces in sim mode */
	u64 sim_tx_packets_0;
	u64 sim_rx_packets_0;
	u64 sim_tx_packets_1;
	u64 sim_rx_packets_1;

	/* Ring buffer telemetry (sim) */
	u32 ring_head_0;
	u32 ring_tail_0;
	u32 ring_head_1;
	u32 ring_tail_1;
	u64 ring_full_drops;
};

#define NATA_IOC_MAGIC 'n'
#define NATA_IOC_BIND   _IOW(NATA_IOC_MAGIC, 1, struct nata_ioc_bind)
#define NATA_IOC_UNBIND _IO(NATA_IOC_MAGIC, 2)
#define NATA_IOC_STATUS _IOR(NATA_IOC_MAGIC, 3, struct nata_ioc_status)

#ifdef __KERNEL__
/* Function declarations */
int nata_net_init(struct nata_priv *priv);
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf, size_t len, int op);
int check_rx_pending(struct nata_priv *priv, int is_dev0);
/* True if producer head slot is still published (ring full). Caller holds lock. */
int check_tx_full(struct nata_priv *priv, int is_dev0);
int sim_tx_packet(struct nata_priv *priv, struct sk_buff *skb, int is_dev0);
/*
 * Dequeue one published slot into *skbp (caller injects via NAPI).
 * Caller must hold priv->lock. Returns 1 + *skbp set, 0 empty, <0 after consume-drop.
 */
int sim_rx_dequeue(struct nata_priv *priv, struct napi_struct *napi, int is_dev0, struct sk_buff **skbp);
#endif

#endif /* _NATA_H_ */
