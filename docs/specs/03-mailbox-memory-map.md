# 03 — Mailbox Memory Map Specification

**Spec version:** 0.1 (as-built)  
**Status:** IMPLEMENTED (simulation); PARTIAL (FPGA BRAM mirrors size)  
**Normative code:** `module/nata_blk.c`, `module/nata_main.c`, `firmware/rtl/dual_port_ram.v`

---

## 1. Purpose

The **mailbox** is the shared medium that carries NATA packets between the two endpoints. In simulation it is a single `vmalloc` region. On the FPGA target it is dual-ported BRAM of the same capacity and addressing model.

---

## 2. Capacity

| Parameter | Value | Source |
|-----------|-------|--------|
| Total size | **131072 bytes (128 KiB)** | `NATA_MAILBOX_BYTES`, `vmalloc(131072)` |
| Sector size | **512 bytes** | ATA logical sector assumption |
| Sector count | **256** | `131072 / 512` |
| LBA range | **0 … 255** inclusive | Bounds checks use `>= 256` as invalid |
| DWORD width (RTL) | 32 bits | `dual_port_ram` |
| RTL depth | 32768 × 32-bit = 128 KiB | `reg [31:0] ram [0:32767]` |
| Words per sector (RTL) | 128 | `512 / 4`; address `lba * 128 + dword_index` |

---

## 3. Logical split (two half-mailboxes)

The 256 sectors are split into two equal **64 KiB** halves. Each half is the **TX queue of one side** and the **RX queue of the peer**.

```text
Byte offset     LBA        Region name     nata0 view      nata1 view
-------------   ---------  --------------  --------------  --------------
0x00000         0–127      Lower 64 KiB    RX              TX
0x10000         128–255    Upper 64 KiB    TX              RX
```

### 3.1 Initialized LBA bases (`nata_init`)

| Variable | Value | Meaning |
|----------|-------|---------|
| `tx_lba_0` | 128 | nata0 publishes frames here |
| `rx_lba_0` | 0 | nata0 consumes frames from here |
| `tx_lba_1` | 0 | nata1 publishes frames here |
| `rx_lba_1` | 128 | nata1 consumes frames from here |

### 3.2 Cross-link invariant

```text
tx_lba_0 == rx_lba_1 == 128
tx_lba_1 == rx_lba_0 == 0
```

Changing one without the other breaks the datapath. Hardware bind ioctls expose `tx_lba_start` / `rx_lba_start` per host for future non-default maps; **sim mode hard-codes the table above**.

---

## 4. Physical map (byte view)

```text
sim_mailbox / BRAM
+================================================================+
| LBA 0                                                          |
|  +------------------+----------------------------------------+ |
|  | nata_pkt_hdr 16B | Ethernet frame (nata1 → nata0)         | |
|  +------------------+----------------------------------------+ |
|  ... sectors 1..N-1 continue frame if multi-sector ...         |
|                                                                |
| LBA 1 … 127   (remainder of lower half — currently unused      |
|               except as multi-sector overflow from LBA 0)      |
+----------------------------------------------------------------+
| LBA 128                                                        |
|  +------------------+----------------------------------------+ |
|  | nata_pkt_hdr 16B | Ethernet frame (nata0 → nata1)         | |
|  +------------------+----------------------------------------+ |
|  ... multi-sector overflow into LBA 129+ ...                   |
|                                                                |
| LBA 129 … 255  (remainder of upper half)                       |
+================================================================+
```

### 4.1 Queue depth

**Depth = 1 logical packet per direction.**

- The base LBA of each half is always the sole publication point.
- A subsequent TX overwrites header+payload at that base.
- There is no free-running ring index, head/tail, or multi-buffering.

Implication: under high offered load, packets are **lost by overwrite** before the peer RX thread consumes them. TCP recovers via retransmission; UDP shows loss (see performance spec).

### 4.2 Multi-sector frames

Frames needing 2–3 sectors occupy consecutive LBAs starting at the region base:

| Direction | Base | Possible LBA span for max frame |
|-----------|------|----------------------------------|
| nata1 → nata0 | 0 | 0–2 |
| nata0 → nata1 | 128 | 128–130 |

Those overflow sectors are **not** independently sequenced; they are only payload continuation for the header at the base.

---

## 5. Simulation access API

```c
int sim_mailbox_io(struct nata_priv *priv, u64 sector, void *buf,
                   size_t len, int op);
```

| Parameter | Semantics |
|-----------|-----------|
| `sector` | Starting LBA |
| `buf` | Kernel buffer |
| `len` | Byte length |
| `op` | `1` = write (host→mailbox), else read (mailbox→host) |

**Checks:**

1. `sim_mailbox` non-NULL  
2. `sector < 256`  
3. `len <= 131072`  
4. `len <= 131072 - sector*512`  

**Implementation:** `memcpy` to/from `priv->sim_mailbox + sector * 512`. No DMA, no cache flushes beyond barriers used in TX/RX helpers.

> Hot path TX/RX in `sim_tx_packet` / `sim_rx_one_packet` currently **bypass** `sim_mailbox_io` and `memcpy` directly for lower overhead, applying equivalent bounds checks inline.

---

## 6. Dual-port concurrency model

### 6.1 Simulation

- One shared virtual address space.
- **Software lock** `priv->lock` + BH-disabled spinlock for publish/consume.
- Two RX kthreads and softirq/xmit context coordinate via that lock and waitqueues.

### 6.2 FPGA (`dual_port_ram`)

- True dual-port: independent `clk_a`/`clk_b`, `we_*`, `addr_*`, `din_*`, `dout_*`.
- Same clock domain in top-level wiring today (`clk_150mhz` both ports).
- **No arbitration** for same-address write-write collision; design assumes hosts write **disjoint** halves for TX (A writes upper, B writes lower) in the intended operational map.
- Read-during-write behavior: write-first register update in the always block (same-port); cross-port simultaneous access is implementation-defined classic BRAM behavior — not formally constrained in RTL comments.

### 6.3 Intended hardware ownership

| Port | Host | Typical writes | Typical reads |
|------|------|----------------|---------------|
| A | Host PC A | Upper half (its TX) | Lower half (its RX) |
| B | Host PC B | Lower half (its TX) | Upper half (its RX) |

---

## 7. Address translation (RTL)

From `sata_device_ip` during READ/WRITE:

```text
ram_addr = lba_addr[14:0] * 128 + dma_counter[6:0]
```

| Symbol | Meaning |
|--------|---------|
| `lba_addr` | 48-bit LBA parsed from Register H2D FIS (only low bits used) |
| `* 128` | Sector → first DWORD index |
| `dma_counter` | 0…127 within one sector transfer (simplified single-sector path in stub) |

**Note:** The RTL DMA path is a **stub** that models one 512-byte sector window with a 128-beat DWORD counter. Multi-sector and full FIS data FIS sequencing are incomplete. Software sim does multi-sector correctly via byte `memcpy`.

---

## 8. Initialization and teardown

| Event | Behavior |
|-------|----------|
| Module init | `vmalloc(131072)` then `memset(..., 0, 131072)` |
| Module exit | `vfree(sim_mailbox)` |
| Cold boot FPGA | BRAM contents undefined until host writes; software zeros sim |

Zero-filled mailbox: `magic != NATA_MAGIC` → no RX pending.

---

## 9. Future map extensions (non-normative)

Not implemented; reserved for later design:

- Ring of N slots per direction with head/tail LBAs  
- Control sector for credits / link MTU  
- Separate identify/geometry metadata outside packet regions  
- Larger than 128 KiB mailbox if FPGA BRAM budget allows  

Any change to size or split **must** update:

- `NATA_MAILBOX_BYTES` / sector constants  
- Default `tx_lba_*` / `rx_lba_*`  
- RTL `dual_port_ram` depth  
- This document and [02-packet-format.md](02-packet-format.md)

---

## 10. Related

- [02-packet-format.md](02-packet-format.md)  
- [07-fpga-rtl.md](07-fpga-rtl.md)  
