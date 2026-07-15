# 03 — Mailbox Memory Map Specification

**Spec version:** 0.3 (as-built)  
**Status:** IMPLEMENTED (simulation); PARTIAL (FPGA BRAM mirrors size, not ring protocol)  
**Normative code:** `module/nata.h`, `module/nata_blk.c`, `module/nata_main.c`, `firmware/rtl/dual_port_ram.v`

---

## 1. Purpose

The **mailbox** is the shared medium that carries NATA packets between the two endpoints. In simulation it is a single `vmalloc` region. On the FPGA target it is dual-ported BRAM of the same capacity. Software organizes each 64 KiB half as a **32-slot ring buffer**.

---

## 2. Capacity

| Parameter | Value | Source |
|-----------|-------|--------|
| Total size | **131072 bytes (128 KiB)** | `NATA_MAILBOX_BYTES`, `vmalloc(NATA_MAILBOX_BYTES)` |
| Sector size | **512 bytes** | ATA logical sector assumption |
| Sector count | **256** | `131072 / 512` |
| LBA range | **0 … 255** inclusive | Bounds checks use `>= 256` as invalid |
| Ring slots per direction | **32** (`NATA_RING_SLOTS`, power of 2) | `nata.h` |
| Sectors per slot | **4** (`NATA_SLOT_SECTORS`) | 2048 bytes/slot |
| Bytes per half | **64 KiB** | 32 × 4 × 512 |
| DWORD width (RTL) | 32 bits | `dual_port_ram` |
| RTL depth | 32768 × 32-bit = 128 KiB | geometry only; no ring FSM in RTL yet |

---

## 3. Logical split (two half-mailboxes)

```text
Byte offset     LBA        Region name     nata0 view      nata1 view
-------------   ---------  --------------  --------------  --------------
0x00000         0–127      Lower 64 KiB    RX ring         TX ring
0x10000         128–255    Upper 64 KiB    TX ring         RX ring
```

### 3.1 Initialized LBA bases (`nata_init`)

| Variable | Value | Meaning |
|----------|-------|---------|
| `tx_lba_0` | 128 | Base of nata0 TX ring (upper) |
| `rx_lba_0` | 0 | Base of nata0 RX ring (lower = nata1 TX) |
| `tx_lba_1` | 0 | Base of nata1 TX ring (lower) |
| `rx_lba_1` | 128 | Base of nata1 RX ring (upper = nata0 TX) |

### 3.2 Cross-link invariant

```text
tx_lba_0 == rx_lba_1 == 128
tx_lba_1 == rx_lba_0 == 0
```

---

## 4. Per-half ring layout

Within each half, **slot `i`** (i = 0…31) starts at:

```text
slot_lba    = base_lba + i * NATA_SLOT_SECTORS   // base_lba is 0 or 128
slot_offset = slot_lba * 512                     // byte offset in sim_mailbox
```

| Slot | Lower half LBA (nata1 TX) | Upper half LBA (nata0 TX) |
|------|---------------------------|---------------------------|
| 0 | 0–3 | 128–131 |
| 1 | 4–7 | 132–135 |
| … | … | … |
| 31 | 124–127 | 252–255 |

### 4.1 Slot byte layout (2048 bytes)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 4 | `valid` (`u32`): `0` empty, `1` published |
| 4 | 16 | `struct nata_pkt_hdr` (magic starts at **slot+4**) |
| 20 | `len` | Ethernet frame payload |
| 20+len … 2047 | pad | Unused / stale |

Max payload = 2048 − 20 = **2028** bytes ≥ `ETH_FRAME_LEN` (1514).

### 4.2 Head / tail (software indices)

| Ring | Producer (head) | Consumer (tail) | Fields |
|------|-----------------|-----------------|--------|
| Upper (nata0 → nata1) | nata0 TX | nata1 RX | `tx_head_0`, `tx_tail_0` |
| Lower (nata1 → nata0) | nata1 TX | nata0 RX | `tx_head_1`, `tx_tail_1` |

- Indices are `0 … 31` (`& NATA_RING_MASK`).
- **Empty:** slot at `tail` has `valid == 0`.
- **Full:** slot at `head` has `valid == 1` → producer returns `-ENOSPC`; xmit path applies **`NETDEV_TX_BUSY`** + `netif_stop_queue` (no overwrite). Increments `ring_full_drops` as **pressure telemetry** (packet is not dropped).
- Producer: write payload+header, `smp_wmb()`, set `valid=1`, advance head.
- Consumer: observe `valid==1`, `smp_rmb()`, read, clear `valid`, advance tail (always on poison paths too); **wake** producer queue if stopped.

### 4.3 Queue depth

**Depth = 32 packets per direction.** Under sustained flood faster than NAPI drain, the ring fills and further TX returns `-ENOSPC` → `NETDEV_TX_BUSY` until a free slot wakes the queue. No silent overwrite of in-flight slots.

---

## 5. Simulation access API

```c
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf,
                   size_t len, int op);
```

Generic sector R/W with bounds. Hot path TX/RX uses `nata_slot_ptr()` + direct `memcpy`.

---

## 6. Dual-port concurrency model

Unchanged vs prior: software `spin_lock_bh` + barriers in sim; FPGA dual-port BRAM without software ring FSM (hardware path still PARTIAL).

---

## 7. Initialization

| Event | Behavior |
|-------|----------|
| Module init | `vmalloc(NATA_MAILBOX_BYTES)`, zero; head/tail = 0 |
| Cold mailbox | all `valid == 0` → empty rings |

---

## 8. Related

- [02-packet-format.md](02-packet-format.md)  
