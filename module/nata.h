#ifndef _NATA_H_
#define _NATA_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/kthread.h>
#include <linux/skbuff.h>

#define NATA_MAGIC 0x4E415441  /* "NATA" in ASCII */

struct nata_pkt_hdr {
    u32 magic;
    u32 len;
    u32 seq;
    u32 reserved;
};

struct nata_priv {
    struct net_device *netdev0;
    struct net_device *netdev1;
    spinlock_t lock;

    /* Shared Memory Mailbox for Simulation Mode (128KB allocated via vmalloc) */
    u8 *sim_mailbox;

    /* LBA offsets (each block/sector is 512 bytes)
     * Lower 64KB is LBA 0 to 127 (offset 0)
     * Upper 64KB is LBA 128 to 255 (offset 65536)
     */
    u64 tx_lba_0; /* 128 (upper 64KB) */
    u64 rx_lba_0; /* 0   (lower 64KB) */
    u64 tx_lba_1; /* 0   (lower 64KB) */
    u64 rx_lba_1; /* 128 (upper 64KB) */

    /* RX thread management */
    struct task_struct *rx_thread_0;
    struct task_struct *rx_thread_1;
    wait_queue_head_t rx_wait_0;
    wait_queue_head_t rx_wait_1;

    /* State tracking */
    u32 tx_seq_0;
    u32 tx_seq_1;
    u32 last_rx_seq_0;
    u32 last_rx_seq_1;

    /* Stats for nada0 */
    u64 tx_packets_0;
    u64 tx_bytes_0;
    u64 rx_packets_0;
    u64 rx_bytes_0;

    /* Stats for nada1 */
    u64 tx_packets_1;
    u64 tx_bytes_1;
    u64 rx_packets_1;
    u64 rx_bytes_1;

    u64 dropped_blocks;
};

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
};

#define NATA_IOC_MAGIC 'n'
#define NATA_IOC_BIND   _IOW(NATA_IOC_MAGIC, 1, struct nata_ioc_bind)
#define NATA_IOC_UNBIND _IO(NATA_IOC_MAGIC, 2)
#define NATA_IOC_STATUS _IOR(NATA_IOC_MAGIC, 3, struct nata_ioc_status)

/* Function declarations */
int nata_net_init(struct nata_priv *priv);
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf, size_t len, int op);
int check_rx_pending(struct nata_priv *priv, int is_dev0);
int sim_tx_packet(struct nata_priv *priv, struct sk_buff *skb, int is_dev0);
int sim_rx_one_packet(struct nata_priv *priv, int is_dev0);

#endif /* _NATA_H_ */
