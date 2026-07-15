# 02 — Packet Format Specification

**Spec version:** 0.1 (as-built)  
**Status:** IMPLEMENTED  
**Normative code:** `module/nata.h`, `module/nata_blk.c`

---

## 1. Scope

This document defines the **on-mailbox** representation of a single Ethernet frame as stored in the NATA shared mailbox. The same layout is intended for future hardware BRAM sectors.

There is **no** separate on-SATA framing beyond standard ATA sector data once hardware exists; the host writes/reads 512-byte sectors containing this structure.

---

## 2. Constants

| Name | Value | Notes |
|------|-------|--------|
| `NATA_MAGIC` | `0x4E415441` | ASCII `"NATA"` as a 32-bit word |
| Sector size | 512 bytes | Fixed; matches ATA logical sector size used by design |
| Header size | 16 bytes | `sizeof(struct nata_pkt_hdr)` |
| Min frame length (`hdr.len`) | `ETH_HLEN` (14) | Smaller frames rejected on RX |
| Max frame length (`hdr.len`) | `ETH_FRAME_LEN` (1514) | Larger frames rejected on RX |
| Max payload + header | 16 + 1514 = 1530 bytes | → 3 sectors (1536 bytes capacity) |

> **Note:** `ETH_FRAME_LEN` is 1514 on Linux (14-byte Ethernet header + 1500 payload). VLAN-tagged frames larger than this, or jumbo frames, are **out of scope** for the current validator.

---

## 3. Header structure

### 3.1 C definition (canonical)

```c
struct nata_pkt_hdr {
    u32 magic;      /* must be NATA_MAGIC */
    u32 len;        /* Ethernet frame length in bytes (skb->len) */
    u32 seq;        /* monotonically increasing per-TX-direction */
    u32 reserved;   /* must be written as 0; ignore on RX today */
};
```

### 3.2 Binary layout (little-endian host)

All multi-byte fields are **native CPU endianness** as written by `memcpy` of the C struct. On the current x86_64 development target that is **little-endian**.

```text
Offset  Size  Field      Description
------  ----  ---------  ------------------------------------------
0x00    4     magic      0x4E415441 ("NATA")
0x04    4     len        Frame byte count (not including this header)
0x08    4     seq        Sequence number (see §5)
0x0C    4     reserved   Transmitter writes 0
0x10    len   payload    Raw Ethernet frame (dst MAC …), skb->data
0x10+len  *   padding    Implicit zeros / previous data to sector boundary
```

### 3.3 Field rules

| Field | TX rules | RX rules |
|-------|----------|----------|
| `magic` | Always `NATA_MAGIC` | Must equal `NATA_MAGIC` or packet ignored / not pending |
| `len` | `skb->len` as handed to `ndo_start_xmit` | Reject if `< ETH_HLEN` or `> ETH_FRAME_LEN`; count `dropped_blocks`, advance seq |
| `seq` | Pre-increment per direction (`++tx_seq_*`) | Pending if `seq != last_rx_seq_*`; after process, `last_rx_seq = seq` |
| `reserved` | Zero | Not checked |
| payload | Exact `skb->data[0 .. len-1]` | Copied into new `sk_buff`; protocol via `eth_type_trans` |

---

## 4. Sector packing

### 4.1 Size calculation

```text
total_len       = sizeof(hdr) + len          // 16 + frame_len
sectors_needed  = ceil(total_len / 512)
                = (total_len + 511) / 512    // integer form used in code
```

| Example frame | total_len | sectors_needed |
|---------------|-----------|----------------|
| Min Ethernet (14 B) | 30 | 1 |
| ARP (~42 B typical) | 58 | 1 |
| 64 B frame | 80 | 1 |
| 1500 B IP payload + 14 B eth = 1514 | 1530 | 3 |
| ICMP echo request ~98 B | ~114 | 1 |

### 4.2 Placement

A packet for a given direction is written starting at the **base LBA** of that direction’s TX region (see [03-mailbox-memory-map.md](03-mailbox-memory-map.md)):

```text
byte_offset = base_lba * 512
[ header | payload | (padding to end of last sector used) ]
```

Padding is **not** explicitly zeroed on TX; only `hdr` and payload are written. Receivers must not read past `hdr.len`.

### 4.3 Bounds checks (TX)

TX fails (`-EINVAL`) if any of:

- `base_lba >= 256` (mailbox sector count)
- `sectors_needed > 256`
- `base_lba + sectors_needed > 256`

For the default split (128 sectors per half), a full-size 3-sector frame still fits at LBA 0 or LBA 128.

---

## 5. Sequence numbers

### 5.1 Per-direction counters

| Counter | Owner | Initial value | Update |
|---------|-------|---------------|--------|
| `tx_seq_0` | nata0 TX | 0 | Incremented **before** write (`++`) so first packet is seq `1` |
| `tx_seq_1` | nata1 TX | 0 | Same |
| `last_rx_seq_0` | nata0 RX | 0 | Set to accepted (or dropped-but-consumed) header seq |
| `last_rx_seq_1` | nata1 RX | 0 | Same |

### 5.2 Pending detection

```text
pending = (magic == NATA_MAGIC) && (seq != last_rx_seq_for_this_side)
```

There is **no** wrap-around handling special case: after `2^32 - 1` packets, seq wraps naturally; equality still detects “same as last consumed”.

### 5.3 Drop and consume policy

On invalid `len` or sector overflow or `skb` alloc failure or `NET_RX_DROP`:

1. `dropped_blocks++`
2. `last_rx_seq = hdr.seq`  (**consume** so RX thread does not spin)
3. Return error / no inject

This is intentional: a poisoned slot must not pin a CPU.

---

## 6. Publication and observation order (memory model)

Correctness under concurrent TX/RX depends on barrier order, not only the spinlock.

### 6.1 Transmitter (publisher)

```text
1. Write payload bytes at (offset + 16)
2. smp_wmb()                          // all payload stores visible first
3. Write header (magic, len, seq, 0) at offset
```

Observers that see a **new** `seq` with valid `magic` are guaranteed to observe the matching payload for that publication (on architectures where `smp_wmb`/`smp_rmb` pair as used).

### 6.2 Receiver (observer)

```text
1. Read header
2. Validate magic, seq, len, sector span
3. smp_rmb()                          // pair with TX wmb
4. Copy payload of hdr.len bytes
5. Inject via netif_rx / eth_type_trans
6. Update last_rx_seq
```

### 6.3 Mutual exclusion

`priv->lock` (`spinlock_t`) is held for the full TX publish path and full RX consume path (`spin_lock_bh`). Waitqueue checks (`check_rx_pending`) read the header **without** the lock; they only decide whether to wake work. The lock serializes header/payload pairs against concurrent TX.

---

## 7. Ethernet payload contents

| Property | Specification |
|----------|----------------|
| Layer | Full Ethernet II frame as presented by Linux networking |
| Includes | Dest MAC, src MAC, EtherType, payload |
| FCS | **Not** included (Linux netdevs do not pass FCS in `skb` for TX) |
| Source MAC | Random per netdev at registration (`eth_hw_addr_random`) unless changed by userspace |
| Dest MAC | As filled by stack (ARP neighbor, etc.) |

NATA does **not** rewrite MACs, insert VLAN tags, or compress headers.

---

## 8. Wire diagram (one direction)

```text
  nata0 TX  ──────────────────────────►  nata1 RX
  (upper region LBA 128)                 (reads LBA 128)

  Byte stream at LBA 128 * 512:
  +--------+------------------+---------- - - -+
  | header | Ethernet frame   | pad to sector  |
  | 16 B   | len bytes        | boundary       |
  +--------+------------------+---------- - - -+
     ^
     magic=NATA  len  seq  reserved=0
```

Reverse direction uses lower region (LBA 0) with nata1 as publisher.

---

## 9. Non-features (explicit)

| Feature | Status |
|---------|--------|
| Multi-frame ring | Not present |
| ACK / credit field | Not present (`reserved` unused) |
| Checksum over header+payload | Not present (rely on Ethernet/IP/TCP) |
| Compression / aggregation | Not present |
| Big-endian portable on-disk format | Not defined; native endian only |
| Version field in header | Not present |

---

## 10. Compliance checklist (implementation)

- [x] 16-byte header with magic `0x4E415441`
- [x] Payload = raw Ethernet frame
- [x] Sector ceiling math `(total + 511) / 512`
- [x] Payload-before-header publish with `smp_wmb`
- [x] RX length window `[ETH_HLEN, ETH_FRAME_LEN]`
- [x] Sequence consume on drop
- [ ] Explicit zero-pad of unused sector bytes
- [ ] Portable endian / magic byte-swap policy for non-x86

---

## 11. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md) — where packets sit  
- [04-kernel-module.md](04-kernel-module.md) — call graph  
