# 08 — Performance Specification (Simulation)

**Spec version:** 0.5 (as-built)  
**Status:** MEASURED (lab re-run 2026-07-15 after 32-slot + `NETDEV_TX_BUSY`)  
**Mode:** In-kernel mailbox simulation only — **not** SATA PHY throughput  

---

## 1. Scope

```text
netdev TX → spin_lock_bh → ring enqueue → (stop queue if full) → napi_schedule(peer)
  → softirq NAPI poll → short lock dequeue → wake producer if stopped → napi_gro_receive
```

Numbers bound software encapsulation with a **32-slot ring per direction**, **per-netdev NAPI**, and **TX backpressure**. They are not SATA line rate.

---

## 2. Lab environment

| Item | Value |
|------|--------|
| Date | 2026-07-15 (post 32-slot + BUSY) |
| Kernel | Linux 6.8.0-134-generic |
| Host | `ubuntu` (AMD EPYC-Milan class vCPU lab) |
| Module | `nata.ko` sim, **32×4-sector rings**, **NAPI RX**, **`NETDEV_TX_BUSY`** |
| Topology | netns `nata-a` ↔ `nata-b` |
| Tools | `ping`, `iperf3`, `scripts/nata-bench-once.sh` |

---

## 3. Latency (ICMP, 50 packets)

| Metric | Value |
|--------|-------|
| Loss | **0%** |
| RTT min / avg / max | **0.023 / 0.037 / 0.057 ms** |

Single-packet latency remains well under a millisecond (same-host path). Slightly better than the prior 8-slot NAPI baseline (~0.05 ms avg).

---

## 4. TCP throughput (iperf3, 10 s)

| Direction | Bitrate | Retransmits (10 s) |
|-----------|---------|---------------------|
| A → B | **~7.26 Gbit/s** (sender = receiver) | **0** |
| B → A (`-R`) | **~8.02 Gbit/s** | **0** |

### Evolution on this host (same project day)

| Metric | Single-slot kthread | 8-slot + kthread | 8-slot + NAPI | **32-slot + BUSY** |
|--------|---------------------|------------------|---------------|---------------------|
| TCP goodput | ~0.60 Gbit/s | ~3.7 Gbit/s | ~4.4–4.8 Gbit/s | **~7.3–8.0 Gbit/s** |
| TCP retransmits / 10 s | ~164k | ~55–58k | ~53–57k | **0** |

Depth 32 plus stop/wake backpressure removed mailbox-induced loss as a TCP stall source: **zero retransmits** in both directions over 10 s floods, and goodput rose ~60–70% vs 8-slot NAPI.

---

## 5. UDP (iperf3 unlimited, 10 s)

| Side | Result |
|------|--------|
| Sender offered | **~2.34 Gbit/s** |
| Receiver goodput | **~2.33 Gbit/s** |
| Loss | **~0.012%** (250 / 2 015 970 datagrams) |

### Evolution

| Metric | Single-slot | 8-slot kthread | 8-slot NAPI | **32-slot + BUSY** |
|--------|-------------|----------------|-------------|---------------------|
| UDP goodput | ~2.23 Gbit/s | ~3.54 Gbit/s | ~2.49 Gbit/s | **~2.33 Gbit/s** |
| UDP loss (unlimited) | ~45% | ~3.4% | ~0.07% | **~0.012%** |

UDP offer under `-b 0` is still CPU/scheduling limited (not a pure apples-to-apples goodput race). Prefer loss% and TCP goodput as primary trend lines.

After the full suite, `natactl status` showed **`ring_full_drops` = 0**, **`dropped_blocks` = 0**, rings drained (head == tail both directions).

---

## 6. Summary table (normative for README)

| Metric | Result (32-slot + NAPI + BUSY) |
|--------|--------------------------------|
| ICMP RTT avg | **~0.037 ms** |
| TCP goodput | **~7.3–8.0 Gbit/s** either direction |
| TCP retransmits / 10 s | **0** |
| UDP goodput (`-b 0`) | **~2.3 Gbit/s** |
| UDP loss at unlimited offer | **~0.012%** |
| `ring_full_drops` after suite | **0** |

---

## 7. Design limits still in play

| Limit | Impact |
|-------|--------|
| Ring depth 32 | Finite burst absorption; then `NETDEV_TX_BUSY` until free (not exercised under this lab flood) |
| Shared `spin_lock` for both directions | TX and RX serialize briefly on enqueue/dequeue |
| Softirq NAPI budget (default 64) | Poll yields; may reschedule on residual pending |
| Slot size 2048 B | Fits `ETH_FRAME_LEN`; no jumbo frames |
| Same-host sim | Not SATA PHY / dual-host CPU cost |

**Removed vs prior:** dedicated RX kthreads; always-`NETDEV_TX_OK` drop-on-full; 8×8192-byte slots; tens of thousands of TCP retransmits under flood.

---

## 8. Re-measurement

```bash
sudo ./scripts/nata-bench-once.sh
# → /tmp/nata-bench-results.txt
```

Re-run after ring depth, locking, NAPI weight, or TX path changes; update if results move **>15%**. Record `natactl status` `ring_full_drops` after iperf (BUSY pressure telemetry when non-zero).

---

## 9. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [02-packet-format.md](02-packet-format.md)  
- [04-kernel-module.md](04-kernel-module.md)  
