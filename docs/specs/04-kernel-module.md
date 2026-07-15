# 04 — Kernel Module Specification

**Spec version:** 0.4 (as-built)  
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
| RX NAPI | `napi0`, `napi1` (per netdev; no kthreads) |
| Ring | `tx_head_0/1`, `tx_tail_0/1` (32 slots/dir) |
| Observability seq | `tx_seq_0/1` (written into header only) |
| Stats | `tx_packets/bytes_*`, `rx_packets/bytes_*`, `dropped_blocks`, `ring_full_drops` |

Full layout: `module/nata.h` (kernel-only `struct nata_priv`; ioctl structs shared with userspace).

---

## 4. Lifecycle

### 4.1 Init sequence (`nata_init`)

Ordered steps:

1. Log simulation banner messages  
2. `kzalloc(sizeof(struct nata_priv))` → `global_priv`  
3. `spin_lock_init`  
4. Set LBA bases: TX0=128, RX0=0, TX1=0, RX1=128; head/tail = 0  
5. `vmalloc(131072)` mailbox; `memset` zero (all slots empty)  
6. `misc_register(&nata_miscdev)` → `/dev/nata_ctl`  
7. `nata_net_init(priv)` → `netif_napi_add` + register `nata0`, `nata1`  

**Failure unwind:** reverse order (unregister netdev path inside `nata_net_init`, deregister misc, `vfree`, `kfree`).

### 4.2 Exit sequence (`nata_exit`)

1. `misc_deregister`  
2. `unregister_netdev` (runs `ndo_stop` → `napi_disable`) + `netif_napi_del` + `free_netdev` for both NICs  
3. `vfree` mailbox  
4. `kfree` priv  

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
| `ndo_open` | `napi_enable` for this NIC; `netif_start_queue`; return 0 |
| `ndo_stop` | `netif_stop_queue`; `napi_disable` for this NIC |
| `ndo_start_xmit` | `nata_xmit_0` or `nata_xmit_1` |

No `ndo_get_stats64` override — uses core `dev->stats` updated on TX/RX. No ethtool ops. **NAPI is present** (default weight via `netif_napi_add`).

### 5.3 Transmit path

#### `nata_xmit_0` (nata0)

```text
if !priv || !sim_mailbox → kfree_skb, NETDEV_TX_OK
spin_lock_bh
  ret = sim_tx_packet(priv, skb, is_dev0=1)   // note: flag 1 means nata0
  if -ENOSPC:
    ring_full_drops++; netif_stop_queue(dev)
    unlock; napi_schedule(peer); return NETDEV_TX_BUSY  // skb kept by stack
  on success: update dev->stats + priv tx_*_0
              if check_tx_full → netif_stop_queue(dev)   // proactive
  on other fail: dev->stats.tx_errors++
spin_unlock_bh
if success: napi_schedule(&priv->napi1); dev_consume_skb_any(skb)
else: dev_kfree_skb_any(skb)   // invalid frame only
return NETDEV_TX_OK
```

#### `nata_xmit_1` (nata1)

Symmetric via shared `nata_xmit(..., is_dev0=0)`.

#### Backpressure policy

| Condition | Behavior |
|-----------|----------|
| Ring full (`-ENOSPC`) | `NETDEV_TX_BUSY`, `netif_stop_queue`, skb **not** freed, `ring_full_drops++` |
| Last free slot just filled | Proactive `netif_stop_queue` after successful enqueue |
| Slot freed in NAPI | `netif_wake_queue` on the **producer** netdev if stopped |
| Invalid frame | Free skb, `NETDEV_TX_OK`, `tx_errors++` |

### 5.4 `is_dev0` convention (critical)

In `nata_blk.c` helpers, parameter name `is_dev0`:

| Call site | Value | Side |
|-----------|-------|------|
| nata0 TX / RX | `1` | upper TX ring / lower RX ring |
| nata1 TX / RX | `0` | lower TX ring / upper RX ring |

Naming is historical; treat as “operating on nata0 when nonzero”.

### 5.5 Ring TX (`sim_tx_packet`)

Enqueue at `head` of the peer-facing half. If `valid==1` at head, ring is full — return `-ENOSPC` (no overwrite). Else write payload+header, `smp_wmb()`, set `valid=1`, advance head. See [02](02-packet-format.md) and [03](03-mailbox-memory-map.md).

---

## 6. Receive path

### 6.1 NAPI (replaces kthreads)

| NAPI | Netdev | Dequeue flag | Scheduled by |
|------|--------|--------------|--------------|
| `napi0` | `nata0` | `is_dev0=1` | successful TX on nata1 |
| `napi1` | `nata1` | `is_dev0=0` | successful TX on nata0 |

Shared poll (`nata_poll`):

```text
while work < budget:
  spin_lock
    ret = sim_rx_dequeue(...)   // may clear valid + advance tail
    if ret != 0: netif_wake_queue(producer) if stopped
  spin_unlock
  if empty → break
  if error → continue
  eth_type_trans; napi_gro_receive; work++
if work < budget:
  napi_complete_done
  if check_rx_pending → napi_schedule   // race after empty
```

**Lock scope:** only ring dequeue under `priv->lock`. Stack inject runs **outside** the lock so TX can keep filling slots. Producer wake runs under the lock after a freed slot.

### 6.2 Pending check (`check_rx_pending`)

Lock-free peek at **tail** slot of the RX ring (NAPI reschedule after complete):

```text
return nata_slot_read_valid(slot_at_tail) == 1
```

### 6.3 Packet dequeue (`sim_rx_dequeue`)

1. If tail slot `valid != 1` → return 0 (empty)  
2. `smp_rmb()`; copy header from slot+4  
3. Validate magic/len; on failure: drop count, clear valid, advance tail, return &lt;0  
4. `dev_alloc_skb`; copy payload from slot+20; on OOM: consume slot, return &lt;0  
5. Clear valid, advance tail; update stats  
6. Return skb (caller: `eth_type_trans` + `napi_gro_receive`)  

**Injection API:** `napi_gro_receive` (NAPI softirq). Slot is freed **before** inject so the producer can reuse it promptly.

### 6.4 Schedule sources

| Event | Action |
|-------|--------|
| Successful TX on nata0 | `napi_schedule(&napi1)` |
| Successful TX on nata1 | `napi_schedule(&napi0)` |
| Residual pending after `napi_complete_done` | `napi_schedule` same NAPI |

`napi_schedule` is a no-op while NAPI is disabled (`ndo_stop` / interface down).

---

## 7. Encapsulation helpers (`nata_blk.c`)

| Function | Role |
|----------|------|
| `sim_mailbox_io` | Generic sector R/W with bounds (available; hot path uses direct memcpy) |
| `check_rx_pending` | NAPI reschedule predicate (`valid` at tail) |
| `sim_tx_packet` | Ring enqueue at head; `-ENOSPC` if full (xmit → BUSY) |
| `sim_rx_dequeue` | Ring dequeue at tail into skb; no stack inject |

See [02-packet-format.md](02-packet-format.md) for field-level rules.

---

## 8. Statistics

### 8.1 Per-priv counters

| Counter | Increment condition |
|---------|---------------------|
| `tx_packets_0/1`, `tx_bytes_0/1` | Successful `sim_tx_packet` |
| `ring_full_drops` | TX saw full ring (`-ENOSPC` → `NETDEV_TX_BUSY`; not a packet drop) |
| `rx_packets_0/1`, `rx_bytes_0/1` | Successful `sim_rx_dequeue` (before GRO inject) |
| `dropped_blocks` | Bad len, sector overflow, skb alloc fail (not TX busy) |

### 8.2 `net_device->stats`

| Field | Update |
|-------|--------|
| `tx_packets`, `tx_bytes` | On successful TX |
| `tx_errors` | On `sim_tx_packet` failure other than `-ENOSPC` |
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
| `ndo_start_xmit` | `spin_lock_bh(&priv->lock)` for enqueue only; `napi_schedule` after unlock |
| NAPI poll | `spin_lock(&priv->lock)` for dequeue only; inject outside lock |
| `check_rx_pending` | No lock (racy peek; OK for reschedule) |
| Ioctl status | `spin_lock_bh` while copying counters |
| Mailbox bytes | Not independently atomic; rely on lock + barriers |

TX uses `spin_lock_bh` because xmit can run in BH/softirq. NAPI already runs in softirq, so poll uses plain `spin_lock`. Holding the lock across `napi_gro_receive` is intentionally avoided.

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
| `pr_info` | Init banner, sim bind refusal, netdev register success (NAPI RX) |
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
      → nata_xmit_0 / nata_xmit
        → sim_tx_packet (enqueue upper ring slot)
        → if full: NETDEV_TX_BUSY + stop_queue
        → else: napi_schedule(napi1); consume skb

softirq / nata_poll (napi1)
  → while budget: sim_rx_dequeue under short lock
  → wake nata0 TX queue if stopped
  → napi_gro_receive
      → IP stack peer
```

---

## 15. Related

- [05-control-plane.md](05-control-plane.md)  
- [06-simulation-and-netns.md](06-simulation-and-netns.md)  
