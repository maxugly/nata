#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include "nata.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NATA Engineering Taskforce");
MODULE_DESCRIPTION("Not Advanced Technology Attachment - Virtual IP over SATA Emulation");
MODULE_VERSION("1.0");

static int target_ata_port = -1;
module_param(target_ata_port, int, 0444);
MODULE_PARM_DESC(target_ata_port, "Target ATA port to bind to (-1 for software-defined simulation mode)");

static struct nata_priv *global_priv;

/* The RX polling/receiver thread for nata0 */
static int nata_rx_thread_0(void *data)
{
    struct nata_priv *priv = data;

    pr_info("NATA: RX thread for nata0 started.\n");
    while (!kthread_should_stop()) {
        /* Wait until there is a new packet at rx_lba_0 (lower 64KB) or we are stopping */
        wait_event_interruptible(priv->rx_wait_0,
            kthread_should_stop() || check_rx_pending(priv, 1));

        if (kthread_should_stop())
            break;

        /* Share priv->lock with TX so mailbox header/payload pairs are atomic */
        spin_lock_bh(&priv->lock);
        sim_rx_one_packet(priv, 1); /* 1 for dev0 (nata0) */
        spin_unlock_bh(&priv->lock);
    }
    pr_info("NATA: RX thread for nata0 stopped.\n");
    return 0;
}

/* The RX polling/receiver thread for nata1 */
static int nata_rx_thread_1(void *data)
{
    struct nata_priv *priv = data;

    pr_info("NATA: RX thread for nata1 started.\n");
    while (!kthread_should_stop()) {
        /* Wait until there is a new packet at rx_lba_1 (upper 64KB) or we are stopping */
        wait_event_interruptible(priv->rx_wait_1,
            kthread_should_stop() || check_rx_pending(priv, 0));

        if (kthread_should_stop())
            break;

        spin_lock_bh(&priv->lock);
        sim_rx_one_packet(priv, 0); /* 0 for dev1 (nata1) */
        spin_unlock_bh(&priv->lock);
    }
    pr_info("NATA: RX thread for nata1 stopped.\n");
    return 0;
}

static long nata_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct nata_priv *priv = global_priv;

    if (!priv)
        return -ENODEV;

    switch (cmd) {
    case NATA_IOC_BIND:
    case NATA_IOC_UNBIND:
        if (target_ata_port == -1) {
            pr_info("NATA: Bind/Unbind requested, but Simulation Mode is active.\n");
            return -EOPNOTSUPP;
        }
        return -EINVAL;

    case NATA_IOC_STATUS: {
        struct nata_ioc_status status;
        memset(&status, 0, sizeof(status));

        spin_lock_bh(&priv->lock);
        status.is_bound = 0;
        status.is_sim_mode = 1;
        strncpy(status.bdev_path, "SIMULATED_MAILBOX_RAM", sizeof(status.bdev_path) - 1);
        
        status.tx_lba_start = priv->tx_lba_0;
        status.rx_lba_start = priv->rx_lba_0;
        
        /* Copy interface 0 (nata0) stats as main stats */
        status.tx_packets = priv->tx_packets_0;
        status.tx_bytes = priv->tx_bytes_0;
        status.rx_packets = priv->rx_packets_0;
        status.rx_bytes = priv->rx_bytes_0;
        status.dropped_blocks = priv->dropped_blocks;
        status.interrupt_counts = priv->tx_packets_0 + priv->tx_packets_1; // simulated interrupt triggers

        /* Copy detailed simulation stats */
        status.sim_tx_packets_0 = priv->tx_packets_0;
        status.sim_rx_packets_0 = priv->rx_packets_0;
        status.sim_tx_packets_1 = priv->tx_packets_1;
        status.sim_rx_packets_1 = priv->rx_packets_1;
        spin_unlock_bh(&priv->lock);

        if (copy_to_user((void __user *)arg, &status, sizeof(status)))
            return -EFAULT;
        break;
    }

    default:
        return -EINVAL;
    }

    return 0;
}

static const struct file_operations nata_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = nata_ioctl,
    .compat_ioctl = nata_ioctl,
};

static struct miscdevice nata_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "nata_ctl",
    .fops = &nata_fops,
};

static int __init nata_init(void)
{
    int ret;

    pr_info("NATA: Initializing Software-Defined Simulation Environment...\n");
    pr_info("NATA: Operating in 100%% Virtual Loopback Mode (no hardware required).\n");

    global_priv = kzalloc(sizeof(struct nata_priv), GFP_KERNEL);
    if (!global_priv)
        return -ENOMEM;

    spin_lock_init(&global_priv->lock);

    /* Configure mailbox LBA offsets:
     * nata0 transmits to upper 64KB (LBA 128), reads from lower 64KB (LBA 0)
     * nata1 transmits to lower 64KB (LBA 0), reads from upper 64KB (LBA 128)
     */
    global_priv->tx_lba_0 = 128;
    global_priv->rx_lba_0 = 0;
    global_priv->tx_lba_1 = 0;
    global_priv->rx_lba_1 = 128;

    /* Allocate persistent memory for simulated BRAM mailbox (128 KB via vmalloc) */
    global_priv->sim_mailbox = vmalloc(131072);
    if (!global_priv->sim_mailbox) {
        kfree(global_priv);
        return -ENOMEM;
    }
    memset(global_priv->sim_mailbox, 0, 131072);

    init_waitqueue_head(&global_priv->rx_wait_0);
    init_waitqueue_head(&global_priv->rx_wait_1);

    /* Spawn packet dispatch threads */
    global_priv->rx_thread_0 = kthread_run(nata_rx_thread_0, global_priv, "nata_rx_0");
    if (IS_ERR(global_priv->rx_thread_0)) {
        ret = PTR_ERR(global_priv->rx_thread_0);
        goto err_free_mailbox;
    }

    global_priv->rx_thread_1 = kthread_run(nata_rx_thread_1, global_priv, "nata_rx_1");
    if (IS_ERR(global_priv->rx_thread_1)) {
        ret = PTR_ERR(global_priv->rx_thread_1);
        goto err_stop_thread_0;
    }

    ret = misc_register(&nata_miscdev);
    if (ret) {
        pr_err("NATA: Failed to register control character device.\n");
        goto err_stop_thread_1;
    }

    ret = nata_net_init(global_priv);
    if (ret != 0) {
        goto err_dereg_misc;
    }

    return 0;

err_dereg_misc:
    misc_deregister(&nata_miscdev);
err_stop_thread_1:
    kthread_stop(global_priv->rx_thread_1);
err_stop_thread_0:
    kthread_stop(global_priv->rx_thread_0);
err_free_mailbox:
    vfree(global_priv->sim_mailbox);
    kfree(global_priv);
    return ret;
}

static void __exit nata_exit(void)
{
    pr_info("NATA: Tearing down virtual simulation loopback bridge.\n");

    misc_deregister(&nata_miscdev);

    if (global_priv) {
        /* Stop receiver threads */
        if (global_priv->rx_thread_0)
            kthread_stop(global_priv->rx_thread_0);
        if (global_priv->rx_thread_1)
            kthread_stop(global_priv->rx_thread_1);

        /* Unregister virtual network devices */
        if (global_priv->netdev0) {
            unregister_netdev(global_priv->netdev0);
            free_netdev(global_priv->netdev0);
        }
        if (global_priv->netdev1) {
            unregister_netdev(global_priv->netdev1);
            free_netdev(global_priv->netdev1);
        }

        /* Free mailbox memory allocated via vmalloc */
        if (global_priv->sim_mailbox) {
            vfree(global_priv->sim_mailbox);
        }
        kfree(global_priv);
    }
}

module_init(nata_init);
module_exit(nata_exit);