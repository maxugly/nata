#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/capability.h>
#include "nata.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NATA Engineering Taskforce");
MODULE_DESCRIPTION("Not Advanced Technology Attachment - Virtual IP over SATA Emulation");
MODULE_VERSION("1.0");

static int target_ata_port = -1;
module_param(target_ata_port, int, 0444);
MODULE_PARM_DESC(target_ata_port, "Target ATA port to bind to (-1 for software-defined simulation mode)");

static struct nata_priv *global_priv;

static long nata_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct nata_priv *priv = global_priv;

	if (!priv)
		return -ENODEV;

	switch (cmd) {
	case NATA_IOC_BIND:
	case NATA_IOC_UNBIND:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

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
		strncpy(status.bdev_path, "SIMULATED_MAILBOX_RAM",
			sizeof(status.bdev_path) - 1);

		status.tx_lba_start = priv->tx_lba_0;
		status.rx_lba_start = priv->rx_lba_0;

		/* Copy interface 0 (nata0) stats as main stats */
		status.tx_packets = priv->tx_packets_0;
		status.tx_bytes = priv->tx_bytes_0;
		status.rx_packets = priv->rx_packets_0;
		status.rx_bytes = priv->rx_bytes_0;
		status.dropped_blocks = priv->dropped_blocks;
		/* synthetic AN count: each successful TX is one peer NAPI schedule */
		status.interrupt_counts = priv->tx_packets_0 + priv->tx_packets_1;

		status.sim_tx_packets_0 = priv->tx_packets_0;
		status.sim_rx_packets_0 = priv->rx_packets_0;
		status.sim_tx_packets_1 = priv->tx_packets_1;
		status.sim_rx_packets_1 = priv->rx_packets_1;

		status.ring_head_0 = priv->tx_head_0;
		status.ring_tail_0 = priv->tx_tail_0;
		status.ring_head_1 = priv->tx_head_1;
		status.ring_tail_1 = priv->tx_tail_1;
		status.ring_full_drops = priv->ring_full_drops;
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

	/*
	 * Mailbox LBA bases (32-slot ring starts at each half base):
	 * nata0 TX ring: upper 64KB LBA 128; nata0 RX: lower 64KB LBA 0
	 * nata1 TX ring: lower 64KB LBA 0;   nata1 RX: upper 64KB LBA 128
	 */
	global_priv->tx_lba_0 = 128;
	global_priv->rx_lba_0 = 0;
	global_priv->tx_lba_1 = 0;
	global_priv->rx_lba_1 = 128;

	/* Ring indices zeroed by kzalloc; explicit for clarity */
	global_priv->tx_head_0 = 0;
	global_priv->tx_tail_0 = 0;
	global_priv->tx_head_1 = 0;
	global_priv->tx_tail_1 = 0;

	/* Simulated dual-port BRAM mailbox (128 KiB; geometry locked in nata.h) */
	global_priv->sim_mailbox = vmalloc(NATA_MAILBOX_BYTES);
	if (!global_priv->sim_mailbox) {
		kfree(global_priv);
		return -ENOMEM;
	}
	memset(global_priv->sim_mailbox, 0, NATA_MAILBOX_BYTES);

	ret = misc_register(&nata_miscdev);
	if (ret) {
		pr_err("NATA: Failed to register control character device.\n");
		goto err_free_mailbox;
	}

	ret = nata_net_init(global_priv);
	if (ret != 0)
		goto err_dereg_misc;

	return 0;

err_dereg_misc:
	misc_deregister(&nata_miscdev);
err_free_mailbox:
	vfree(global_priv->sim_mailbox);
	kfree(global_priv);
	global_priv = NULL;
	return ret;
}

static void __exit nata_exit(void)
{
	pr_info("NATA: Tearing down virtual simulation loopback bridge.\n");

	misc_deregister(&nata_miscdev);

	if (global_priv) {
		/* Unregister virtual network devices (ndo_stop disables NAPI) */
		if (global_priv->netdev0) {
			unregister_netdev(global_priv->netdev0);
			netif_napi_del(&global_priv->napi0);
			free_netdev(global_priv->netdev0);
		}
		if (global_priv->netdev1) {
			unregister_netdev(global_priv->netdev1);
			netif_napi_del(&global_priv->napi1);
			free_netdev(global_priv->netdev1);
		}

		if (global_priv->sim_mailbox)
			vfree(global_priv->sim_mailbox);
		kfree(global_priv);
		global_priv = NULL;
	}
}

module_init(nata_init);
module_exit(nata_exit);
