#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "../module/nata.h"

#define NATA_CTL_DEV "/dev/nata_ctl"

static void print_usage(const char *prog)
{
    fprintf(stderr, "NATA (Not Advanced Technology Attachment) Management Tool\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s bind <block_device> [tx_lba] [rx_lba]\n", prog);
    fprintf(stderr, "  %s unbind\n", prog);
    fprintf(stderr, "  %s status\n", prog);
}

static int do_bind(const char *bdev_path, u64 tx_lba, u64 rx_lba)
{
    int fd;
    struct nata_ioc_bind bind_data;

    fd = open(NATA_CTL_DEV, O_RDWR);
    if (fd < 0) {
        perror("Error opening " NATA_CTL_DEV ". Is nata.ko loaded?");
        return 1;
    }

    memset(&bind_data, 0, sizeof(bind_data));
    strncpy(bind_data.bdev_path, bdev_path, sizeof(bind_data.bdev_path) - 1);
    bind_data.tx_lba_start = tx_lba;
    bind_data.rx_lba_start = rx_lba;

    if (ioctl(fd, NATA_IOC_BIND, &bind_data) < 0) {
        if (errno == EOPNOTSUPP) {
            fprintf(stderr, "Operation not supported: NATA is running in Simulation Loopback Mode.\n");
        } else {
            perror("Failed to bind storage device");
        }
        close(fd);
        return 1;
    }

    printf("Successfully bound virtual NIC to %s (TX LBA: %llu, RX LBA: %llu).\n",
           bdev_path, (unsigned long long)tx_lba, (unsigned long long)rx_lba);

    close(fd);
    return 0;
}

static int do_unbind(void)
{
    int fd;

    fd = open(NATA_CTL_DEV, O_RDWR);
    if (fd < 0) {
        perror("Error opening " NATA_CTL_DEV);
        return 1;
    }

    if (ioctl(fd, NATA_IOC_UNBIND) < 0) {
        if (errno == EOPNOTSUPP) {
            fprintf(stderr, "Operation not supported: NATA is running in Simulation Loopback Mode.\n");
        } else {
            perror("Failed to unbind device");
        }
        close(fd);
        return 1;
    }

    printf("Successfully unbound backing storage device.\n");
    close(fd);
    return 0;
}

static int do_status(void)
{
    int fd;
    struct nata_ioc_status status;

    fd = open(NATA_CTL_DEV, O_RDWR);
    if (fd < 0) {
        perror("Error opening " NATA_CTL_DEV ". Is nata.ko loaded?");
        return 1;
    }

    memset(&status, 0, sizeof(status));
    if (ioctl(fd, NATA_IOC_STATUS, &status) < 0) {
        perror("Failed to query status");
        close(fd);
        return 1;
    }

    printf("==================================================\n");
    printf("     NATA Protocol Link Status & Telemetry        \n");
    printf("==================================================\n");
    if (status.is_sim_mode) {
        printf("Mode:             SOFTWARE SIMULATION LOOPBACK\n");
        printf("Link Status:      UP (nata0 <-> nata1 bridged)\n");
        printf("Virtual Mailbox:  %s\n", status.bdev_path);
        printf("--------------------------------------------------\n");
        printf("nata0 Statistics:\n");
        printf("  TX Packets:     %llu\n", (unsigned long long)status.sim_tx_packets_0);
        printf("  RX Packets:     %llu\n", (unsigned long long)status.sim_rx_packets_0);
        printf("nata1 Statistics:\n");
        printf("  TX Packets:     %llu\n", (unsigned long long)status.sim_tx_packets_1);
        printf("  RX Packets:     %llu\n", (unsigned long long)status.sim_rx_packets_1);
        printf("--------------------------------------------------\n");
        printf("Dropped Blocks:   %llu\n", (unsigned long long)status.dropped_blocks);
        printf("Sim Interrupts:   %llu\n", (unsigned long long)status.interrupt_counts);
    } else if (status.is_bound) {
        printf("Mode:             PHYSICAL HARDWARE BIND\n");
        printf("Link Status:      UP (Bound)\n");
        printf("Backing Device:   %s\n", status.bdev_path);
        printf("TX Mailbox LBA:   %llu\n", (unsigned long long)status.tx_lba_start);
        printf("RX Mailbox LBA:   %llu\n", (unsigned long long)status.rx_lba_start);
        printf("TX Packets:       %llu\n", (unsigned long long)status.tx_packets);
        printf("TX Bytes:         %llu\n", (unsigned long long)status.tx_bytes);
        printf("RX Packets:       %llu\n", (unsigned long long)status.rx_packets);
        printf("RX Bytes:         %llu\n", (unsigned long long)status.rx_bytes);
        printf("Dropped Blocks:   %llu\n", (unsigned long long)status.dropped_blocks);
        printf("Interrupts:       %llu\n", (unsigned long long)status.interrupt_counts);
    } else {
        printf("Link Status:      DOWN (Unbound)\n");
    }
    printf("==================================================\n");

    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "bind") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'bind' requires a block device path.\n");
            print_usage(argv[0]);
            return 1;
        }
        u64 tx_lba = 0;
        u64 rx_lba = 100;
        if (argc >= 4) {
            tx_lba = strtoull(argv[3], NULL, 10);
        }
        if (argc >= 5) {
            rx_lba = strtoull(argv[4], NULL, 10);
        }
        return do_bind(argv[2], tx_lba, rx_lba);
    } else if (strcmp(argv[1], "unbind") == 0) {
        return do_unbind();
    } else if (strcmp(argv[1], "status") == 0) {
        return do_status();
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
}
