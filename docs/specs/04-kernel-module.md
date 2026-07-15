# 04 — Kernel Module Specification

**Spec version:** 0.1 (as-built)  
**Status:** IMPLEMENTED (simulation datapath)  
**Sources:** `module/nata_main.c`, `module/nata_net.c`, `module/nata_blk.c`, `module/nata.h`, `module/Makefile`

---

## 1. Identity

| Property | Value |
|----------|--------|
| Module name | `nata` |
| Object | `nata.ko` |
| Version | `1.0` |
| License | GPL |
| Author string | NATA Engineering Taskforce |
| Description | Not Advanced Technology Attachment - Virtual IP over SATA Emulation |
| Build | Out-of-tree: `make -C /lib/modules/$(uname -r)/build M=$PWD modules` |
| Objects linked | `nata_main.o` + `nata_net.o` + `nata_blk.o` → `nata.o` |

---

## 2. Module parameters

| Name | Type | Perm | Default | Description |
|------|------|------|---------|-------------|
| `target_ata_port` | `int` | `0444` (read-only after load) | `-1` | Target ATA port for hardware bind. **`-1` = software simulation mode** (only fully supported path). |

```bash
insmod nata.ko target_ata_port=-1
```

Non-`-1` values do **not** currently implement a working bind path; bind ioctls still fail.

---

## 3. Global state

Single global pointer:

```c
static struct nata_priv *global_priv;
```

One module instance supports **exactly one** dual-NIC simulation (one mailbox, two netdevs). Multi-instance / multi-bridge is not supported.

### 3.1 `struct nata_priv` (summary)

| Group | Fields |
|-------|--------|
| Netdevs | `netdev0`, `netdev1` |
| Sync | `lock` (spinlock) |
| Mailbox | `sim_mailbox`, `tx_lba_0/1`, `rx_lba_0/1` |
| RX threads | `rx_thread_0/1`, `rx_wait_0/1` |
| Sequence | `tx_seq_0/1`, `last_rx_seq_0/1` |
| Stats | `tx_packets/bytes_*`, `rx_packets/bytes_*`, `dropped_blocks` |

Full layout: `module/nata.h`.

---

## 4. Lifecycle

### 4.1 Init sequence (`nata_init`)

Ordered steps:

1. Log simulation banner messages  
2. `kzalloc(sizeof(struct nata_priv))` → `global_priv`  
3. `spin_lock_init`  
4. Set LBA bases: TX0=128, RX0=0, TX1=0, RX1=128  
5. `vmalloc(131072)` mailbox; `memset` zero  
6. `init_waitqueue_head` for both RX waits  
7. `kthread_run(nata_rx_thread_0, …, "nata_rx_0")`  
8. `kthread_run(nata_rx_thread_1, …, "nata_rx_1")`  
9. `misc_register(&nata_miscdev)` → `/dev/nata_ctl`  
10. `nata_net_init(priv)` → register `nata0`, `nata1`  

**Failure unwind:** reverse order (unregister netdev path inside `nata_net_init`, deregister misc, stop threads, `vfree`, `kfree`).

### 4.2 Exit sequence (`nata_exit`)

1. `misc_deregister`  
2. `kthread_stop` both RX threads  
3. `unregister_netdev` + `free_netdev` for both NICs  
4. `vfree` mailbox  
5. `kfree` priv  

---

## 5. Network devices

### 5.1 Registration (`nata_net_init`)

| Property | nata0 | nata1 |
|----------|-------|-------|
| Allocator | `alloc_etherdev(sizeof(struct nata_priv *))` | same |
| Name | `"nata0"` | `"nata1"` |
| ops | `nata_netdev_ops_0` | `nata_netdev_ops_1` |
| MAC | `eth_hw_addr_random` | `eth_hw_addr_random` |
| priv | pointer to shared `nata_priv` | same |
| Carrier | `netif_carrier_on` after register | same |

`netdev_priv(dev)` stores `struct nata_priv **` (pointer-sized private area holding address of global priv).

### 5.2 `net_device_ops`

| ndo | Behavior |
|-----|----------|
| `ndo_open` | `netif_start_queue`; return 0 |
| `ndo_stop` | `netif_stop_queue`; return 0 |
| `ndo_start_xmit` | `nata_xmit_0` or `nata_xmit_1` |

No `ndo_get_stats64` override — uses core `dev->stats` updated on TX/RX. No ethtool ops. No NAPI.

### 5.3 Transmit path

#### `nata_xmit_0` (nata0)

```text
if !priv || !sim_mailbox → kfree_skb, NETDEV_TX_OK
spin_lock_bh
  sim_tx_packet(priv, skb, is_dev0=1)   // note: flag 1 means nata0
  on success: update dev->stats + priv tx_*_0; wake_up rx_wait_1
  on fail: dev->stats.tx_errors++
spin_unlock_bh
dev_consume_skb_any(skb)
return NETDEV_TX_OK
```

#### `nata_xmit_1` (nata1)

Symmetric: `sim_tx_packet(..., is_dev0=0)`, wake `rx_wait_0`, stats `_*_1`.

#### Backpressure policy

Always returns **`NETDEV_TX_OK`** and consumes the skb. There is **no** `NETDEV_TX_BUSY`, no queue stop on mailbox full, and no drop of skb on TX error (still consumed). TX errors only increment counters when `sim_tx_packet` fails.

### 5.4 `is_dev0` convention (critical)

In `nata_blk.c` helpers, parameter name `is_dev0`:

| Call site | Value | Side |
|-----------|-------|------|
| nata0 TX / RX | `1` | uses `tx_lba_0` / `rx_lba_0`, seq_0 |
| nata1 TX / RX | `0` | uses `tx_lba_1` / `rx_lba_1`, seq_1 |

Naming is historical; treat as “operating on nata0 when nonzero”.

---

## 6. Receive path

### 6.1 Threads

| Thread name | Function | Wait queue | `sim_rx_one_packet` flag |
|-------------|----------|------------|--------------------------|
| `nata_rx_0` | `nata_rx_thread_0` | `rx_wait_0` | `is_dev0=1` (nata0) |
| `nata_rx_1` | `nata_rx_thread_1` | `rx_wait_1` | `is_dev0=0` (nata1) |

Loop body:

```text
wait_event_interruptible(waitq, kthread_should_stop() || check_rx_pending(...))
if stop → break
spin_lock_bh
sim_rx_one_packet(...)
spin_unlock_bh
```

### 6.2 Pending check (`check_rx_pending`)

Lock-free header peek:

```text
hdr = (nata_pkt_hdr *)(sim_mailbox + rx_lba * 512)
return magic == NATA_MAGIC && seq != last_rx_seq
```

### 6.3 Packet inject (`sim_rx_one_packet`)

1. Copy header  
2. Exit 0 if not new  
3. Validate `len` ∈ `[ETH_HLEN, ETH_FRAME_LEN]`; else drop+consume  
4. Validate sector span  
5. `dev_alloc_skb(len + 2)`; `skb_reserve(2)` for IP align  
6. `smp_rmb`; copy payload  
7. `skb->dev = netdev`; `eth_type_trans`; `netif_rx`  
8. On `NET_RX_DROP`: `dropped_blocks++`, consume seq  
9. On success: update `dev->stats` and priv rx counters; set `last_rx_seq`  

**Injection API:** `netif_rx` (not NAPI `netif_receive_skb` budget loop). Fine for sim; may need revisit under high PPS.

### 6.4 Wake sources

| Event | Wakes |
|-------|-------|
| Successful TX on nata0 | `rx_wait_1` (nata1 RX thread) |
| Successful TX on nata1 | `rx_wait_0` (nata0 RX thread) |
| `kthread_stop` | both (via wait condition) |

There is **no** timer-based poll fallback if a wake is lost; correctness assumes wake always accompanies publish under the same lock that updates the header.

---

## 7. Encapsulation helpers (`nata_blk.c`)

| Function | Role |
|----------|------|
| `sim_mailbox_io` | Generic sector R/W with bounds (available; hot path uses direct memcpy) |
| `check_rx_pending` | Waitqueue predicate |
| `sim_tx_packet` | Build header, payload-first publish |
| `sim_rx_one_packet` | Validate, allocate skb, inject |

See [02-packet-format.md](02-packet-format.md) for field-level rules.

---

## 8. Statistics

### 8.1 Per-priv counters

| Counter | Increment condition |
|---------|---------------------|
| `tx_packets_0/1`, `tx_bytes_0/1` | Successful `sim_tx_packet` |
| `rx_packets_0/1`, `rx_bytes_0/1` | Successful `netif_rx` path |
| `dropped_blocks` | Bad len, sector overflow, skb alloc fail, `NET_RX_DROP` |

### 8.2 `net_device->stats`

| Field | Update |
|-------|--------|
| `tx_packets`, `tx_bytes` | On successful TX |
| `tx_errors` | On `sim_tx_packet` failure |
| `rx_packets`, `rx_bytes` | On successful RX inject |

Not updated: `rx_errors`, `rx_dropped` on netdev (drops go to `dropped_blocks` only).

### 8.3 Simulated interrupt count

Ioctl status sets:

```text
interrupt_counts = tx_packets_0 + tx_packets_1
```

Interpretation: each successful TX is treated as one synthetic AN event for telemetry.

---

## 9. Concurrency and locking

| Context | Locks / notes |
|---------|----------------|
| `ndo_start_xmit` | `spin_lock_bh(&priv->lock)` |
| RX thread consume | `spin_lock_bh(&priv->lock)` |
| `check_rx_pending` | No lock (racy peek; OK for wake) |
| Ioctl status | `spin_lock_bh` while copying counters |
| Mailbox bytes | Not independently atomic; rely on lock + barriers |

`spin_lock_bh` disables softirqs on the local CPU while holding the lock, preventing deadlock with xmit running in softirq/BH context.

---

## 10. Control device (summary)

Registered as misc device:

| Field | Value |
|-------|--------|
| Name | `nata_ctl` |
| Minor | `MISC_DYNAMIC_MINOR` |
| fops | `unlocked_ioctl`, `compat_ioctl` → `nata_ioctl` |

Full ioctl ABI: [05-control-plane.md](05-control-plane.md).

---

## 11. Logging

| Level | Examples |
|-------|----------|
| `pr_info` | Init banner, RX thread start/stop, sim bind refusal, netdev register success |
| `pr_err` | misc register fail, netdev register fail |

Prefix convention in messages: `"NATA: …"`.

---

## 12. Build and deploy artifacts

```text
module/
  Makefile
  nata.h
  nata_main.c
  nata_net.c
  nata_blk.c
  99-nata.rules          # udev: MODE=0660 GROUP=netdev for nata_ctl
```

Install udev rule (manual; not automated by Makefile):

```bash
sudo cp module/99-nata.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

---

## 13. Explicit non-implementation

| Item | Status |
|------|--------|
| libata / AHCI binding | Not coded |
| SCSI WRITE(10)/READ(10) issuance | Not coded |
| MSI/AN IRQ handler | Not coded (wake_up only) |
| Multiple module instances | Not supported |
| netns-aware module logic | Unneeded; netns is pure userspace move of netdevs |
| ethtool, XDP, TSO/GSO offload | Not present |
| IPv6 special casing | None (works if stack uses Ethernet) |

---

## 14. Call graph (simulation datapath)

```text
User/app
  → IP stack
    → dev_queue_xmit(nata0)
      → nata_xmit_0
        → sim_tx_packet (publish upper half)
        → wake_up_interruptible(rx_wait_1)

nata_rx_thread_1
  → wait_event(... check_rx_pending nata1 ...)
  → sim_rx_one_packet
    → netif_rx
      → IP stack peer
```

---

## 15. Related

- [05-control-plane.md](05-control-plane.md)  
- [06-simulation-and-netns.md](06-simulation-and-netns.md)  
