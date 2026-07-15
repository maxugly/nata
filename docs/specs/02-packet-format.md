# 02 — Packet Format Specification

**Spec version:** 0.2 (as-built)  
**Status:** IMPLEMENTED  
**Normative code:** `module/nata.h`, `module/nata_blk.c`

---

## 1. Scope

On-mailbox representation of one Ethernet frame **inside one ring slot**. Same geometry is intended for future hardware BRAM; ring head/tail live in software today.

---

## 2. Constants

| Name | Value | Notes |
|------|-------|--------|
| `NATA_MAGIC` | `0x4E415441` | ASCII `"NATA"` |
| Sector size | 512 bytes | |
| Header size | 16 bytes | `sizeof(struct nata_pkt_hdr)` |
| Valid flag size | 4 bytes | `u32` at slot start |
| Min `hdr.len` | `ETH_HLEN` (14) | |
| Max `hdr.len` | `ETH_FRAME_LEN` (1514) | |
| Slot size | 8192 bytes (16 sectors) | Holds valid + header + max frame easily |
| Ring slots | 8 per direction | See [03-mailbox-memory-map.md](03-mailbox-memory-map.md) |

---

## 3. Slot layout (canonical)

Each ring slot is 8192 bytes. **Valid flag is at byte 0; header (and thus magic) starts at byte 4.**

```text
Slot offset  Size  Field
-----------  ----  ------------------------------------------
0x00         4     valid     0 = empty, 1 = published
0x04         4     magic     0x4E415441 ("NATA")   ← nata_pkt_hdr starts here
0x08         4     len       Ethernet frame length
0x0C         4     seq       Observability counter (not used for pending)
0x10         4     reserved  TX writes 0
0x14         len   payload   skb->data
0x14+len     *     pad       unused remainder of slot
```

```c
struct nata_pkt_hdr {
    u32 magic;
    u32 len;
    u32 seq;
    u32 reserved;
};
/* NATA_SLOT_HDR_OFF = 4, NATA_SLOT_PAYLOAD_OFF = 20 */
```

> **Note:** Pre-ring (v0.1) layout put magic at slot offset 0. Sim-only break is intentional; no deployed hardware depended on the old map.

### 3.1 Field rules

| Field | TX | RX |
|-------|----|----|
| `valid` | Written **last** after `smp_wmb()` | Pending iff `valid == 1` at tail slot |
| `magic` | `NATA_MAGIC` | Must match or slot consumed as drop |
| `len` | `skb->len` | Reject outside `[ETH_HLEN, ETH_FRAME_LEN]` and slot max; consume slot |
| `seq` | `++tx_seq_*` (telemetry) | Not used for pending detection |
| `reserved` | 0 | Ignored |
| payload | `skb->data` | Copied into new skb → `eth_type_trans` / `netif_rx` |

---

## 4. Publication / observation order

### 4.1 Transmitter (producer)

```text
1. If slot[head].valid == 1 → ring full: drop, ring_full_drops++, return -ENOSPC
2. valid = 0 (clear)
3. Write payload at offset 20, header at offset 4
4. smp_wmb()
5. valid = 1
6. head = (head + 1) & 7
```

### 4.2 Receiver (consumer)

```text
1. If slot[tail].valid != 1 → empty
2. smp_rmb()
3. Read header + payload; validate
4. Inject or count drop
5. valid = 0; tail = (tail + 1) & 7   // always after observing valid
```

Poison slots always advance tail so RX cannot busy-loop.

---

## 5. Sequence numbers

`seq` remains a per-direction monotonic counter for observability (ioctl/debug). **Pending detection is solely `valid` + ring tail**, not `last_rx_seq`.

---

## 6. Ethernet payload

Unchanged: full Ethernet II frame without FCS; random MAC at register; no VLAN/jumbo beyond `ETH_FRAME_LEN`.

---

## 7. Compliance checklist

- [x] Valid at slot+0, header at slot+4, payload at slot+20  
- [x] Payload+header before `smp_wmb` before valid=1  
- [x] Full ring drops without overwrite  
- [x] Consume-on-error for bad slots  
- [ ] Portable endian / hardware dual-host wire format freeze  

---

## 8. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [04-kernel-module.md](04-kernel-module.md)  
