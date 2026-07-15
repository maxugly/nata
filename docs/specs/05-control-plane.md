# 05 — Control Plane Specification

**Spec version:** 0.2 (as-built)  
**Status:** IMPLEMENTED (`STATUS` + ring stats); PARTIAL (`BIND`/`UNBIND`)  
**Sources:** `module/nata.h`, `module/nata_main.c`, `tools/natactl.c`, `module/99-nata.rules`

---

## 1. Control device node

| Property | Value |
|----------|--------|
| Path | `/dev/nata_ctl` |
| Type | misc character device |
| Kernel name | `nata_ctl` |
| Minor | dynamic (`MISC_DYNAMIC_MINOR`) |
| fops | `unlocked_ioctl`, `compat_ioctl` |

### 1.1 Permissions (udev)

File: `module/99-nata.rules`

```text
KERNEL=="nata_ctl", MODE="0660", GROUP="netdev"
```

Without this rule, default root-only misc permissions apply (distro-dependent).

---

## 2. Ioctl ABI

### 2.1 Magic and commands

```c
#define NATA_IOC_MAGIC 'n'

#define NATA_IOC_BIND   _IOW(NATA_IOC_MAGIC, 1, struct nata_ioc_bind)
#define NATA_IOC_UNBIND _IO(NATA_IOC_MAGIC, 2)
#define NATA_IOC_STATUS _IOR(NATA_IOC_MAGIC, 3, struct nata_ioc_status)
```

| Command | Direction | Number | Payload |
|---------|-----------|--------|---------|
| `NATA_IOC_BIND` | user → kernel | 1 | `struct nata_ioc_bind` |
| `NATA_IOC_UNBIND` | none | 2 | — |
| `NATA_IOC_STATUS` | kernel → user | 3 | `struct nata_ioc_status` |

Unknown `cmd` → `-EINVAL`.  
If `global_priv == NULL` → `-ENODEV`.

### 2.2 `struct nata_ioc_bind`

```c
struct nata_ioc_bind {
    char bdev_path[128];
    u64  tx_lba_start;
    u64  rx_lba_start;
};
```

| Field | Meaning (intended) |
|-------|---------------------|
| `bdev_path` | Path to backing block device (e.g. `/dev/sdX`) |
| `tx_lba_start` | Host TX mailbox base LBA |
| `rx_lba_start` | Host RX mailbox base LBA |

**Current kernel behavior:**

| Condition | Return |
|-----------|--------|
| `target_ata_port == -1` | `-EOPNOTSUPP` (logs that sim mode is active) |
| otherwise | `-EINVAL` (no bind implementation) |

### 2.3 `NATA_IOC_UNBIND`

Same return table as BIND (sim → `-EOPNOTSUPP`, else `-EINVAL`).

### 2.4 `struct nata_ioc_status`

```c
struct nata_ioc_status {
    char bdev_path[128];
    u64  tx_lba_start;
    u64  rx_lba_start;
    u64  tx_packets;
    u64  tx_bytes;
    u64  rx_packets;
    u64  rx_bytes;
    u64  dropped_blocks;
    u64  interrupt_counts;
    int  is_bound;
    int  is_sim_mode;
    u64  sim_tx_packets_0;
    u64  sim_rx_packets_0;
    u64  sim_tx_packets_1;
    u64  sim_rx_packets_1;
    u32  ring_head_0;
    u32  ring_tail_0;
    u32  ring_head_1;
    u32  ring_tail_1;
    u64  ring_full_drops;
};
```

**Sim-mode fill (current always-on path):**

| Field | Value written |
|-------|----------------|
| `is_bound` | `0` |
| `is_sim_mode` | `1` |
| `bdev_path` | `"SIMULATED_MAILBOX_RAM"` |
| `tx_lba_start` | `priv->tx_lba_0` (128) |
| `rx_lba_start` | `priv->rx_lba_0` (0) |
| `tx_packets` … `rx_bytes` | nata0 counters |
| `dropped_blocks` | shared drop counter (includes ring-full) |
| `interrupt_counts` | `tx_packets_0 + tx_packets_1` |
| `sim_*_0` / `sim_*_1` | per-NIC packet counts |
| `ring_head_0` / `ring_tail_0` | upper half ring indices |
| `ring_head_1` / `ring_tail_1` | lower half ring indices |
| `ring_full_drops` | TX drops when ring full |

`copy_to_user` failure → `-EFAULT`.

---

## 3. Userspace tool: `natactl`

### 3.1 Build

```bash
make -C tools
# produces tools/natactl
```

| Variable | Default |
|----------|---------|
| `CC` | `gcc` |
| `CFLAGS` | `-Wall -Wextra -O2` |
| Install prefix | `/usr/local/bin` (`make install`) |

Includes `../module/nata.h` for ioctl structs (build-time dependency on module headers).

### 3.2 CLI

```text
natactl bind <block_device> [tx_lba] [rx_lba]
natactl unbind
natactl status
```

| Command | Default args | Notes |
|---------|--------------|-------|
| `bind` | `tx_lba=0`, `rx_lba=100` if omitted | Opens `/dev/nata_ctl` O_RDWR; ioctl BIND |
| `unbind` | — | ioctl UNBIND |
| `status` | — | ioctl STATUS; pretty-print |

**Default LBA pair in CLI (`0` / `100`)** differs from **kernel sim map (`128` / `0`)**. That is acceptable only for a future hardware bind path where users choose LBAs; sim mode rejects bind entirely.

### 3.3 Status output modes

| Condition | Presentation |
|-----------|--------------|
| `is_sim_mode` | “SOFTWARE SIMULATION LOOPBACK”; nata0/nata1 TX/RX packet counts; drops; sim interrupts |
| `is_bound` | Physical bind fields (path, LBAs, aggregate counters) — not reachable today |
| else | “DOWN (Unbound)” |

### 3.4 Error strings

- Open fail → suggest module not loaded  
- `EOPNOTSUPP` → “Simulation Loopback Mode” message  

Exit code `1` on failure, `0` on success.

---

## 4. Operational procedures

### 4.1 Query telemetry

```bash
sudo ./tools/natactl status
# or: after udev, as netdev group member
./tools/natactl status
```

### 4.2 Bind (future hardware)

```bash
./tools/natactl bind /dev/sdX 128 0
```

Expected today under sim: **fails** with operation not supported.

---

## 5. ABI stability notes

Breaking changes include:

- Reordering/resizing `struct nata_ioc_*`  
- Changing ioctl numbers or magic  
- Changing string contents that scripts parse (currently none officially)

Additive fields at the **end** of status structs should bump a documented `status_version` in a future revision (not present in v0.1).

---

## 6. Related

- [04-kernel-module.md](04-kernel-module.md)  
- [06-simulation-and-netns.md](06-simulation-and-netns.md)  
