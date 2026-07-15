# 08 — Performance Specification (Simulation)

**Spec version:** 0.2 (as-built)  
**Status:** MEASURED (lab baseline re-run 2026-07-15 after 8-slot ring)  
**Mode:** In-kernel mailbox simulation only — **not** SATA PHY throughput  

---

## 1. Scope

```text
netdev TX → spinlock → ring enqueue → wake → kthread drain → netif_rx
```

Numbers bound software encapsulation with an **8-slot ring per direction**. They are not SATA line rate.

---

## 2. Lab environment

| Item | Value |
|------|--------|
| Date | 2026-07-15 (post ring) |
| Kernel | Linux 6.8.0-124-generic |
| CPU | AMD EPYC-Milan, 8 vCPU |
| Module | `nata.ko` sim, **8×16-sector rings** |
| Topology | netns `nata-a` ↔ `nata-b` |
| Tools | `ping`, `iperf3` 3.16 |

---

## 3. Latency (ICMP, 50 packets)

| Metric | Value |
|--------|-------|
| Loss | **0%** |
| RTT min / avg / max | **0.043 / 0.083 / 0.161 ms** |

Single-packet latency remains sub-millisecond (same host path).

---

## 4. TCP throughput (iperf3, 10 s)

| Direction | Bitrate | Retransmits (10 s) |
|-----------|---------|---------------------|
| A → B | **~3.70 Gbit/s** (sender = receiver) | **~55k** |
| B → A (`-R`) | **~3.77 Gbit/s** | **~58k** |

### Prior single-slot baseline (same host, earlier same day)

| Metric | Single-slot | 8-slot ring |
|--------|-------------|-------------|
| TCP goodput | ~0.60 Gbit/s | **~3.7 Gbit/s** |
| TCP retransmits / 10 s | ~164k | **~55–58k** |

Retransmits are **not** zero: ring depth 8 still underfills vs TCP burst + kthread/`netif_rx` scheduling. They dropped ~3× while throughput rose ~6×. Full elimination needs deeper rings, NAPI, or TX backpressure (`NETDEV_TX_BUSY`).

---

## 5. UDP (iperf3 unlimited, 10 s)

| Side | Result |
|------|--------|
| Sender offered | **~3.67 Gbit/s** |
| Receiver goodput | **~3.54 Gbit/s** |
| Loss | **~3.4%** |

### Prior single-slot baseline

| Metric | Single-slot | 8-slot ring |
|--------|-------------|-------------|
| UDP goodput | ~2.23 Gbit/s | **~3.54 Gbit/s** |
| UDP loss (unlimited) | ~45% | **~3.4%** |

Ring-full drops appear under flood (`ring_full_drops` in `natactl status`) instead of silent overwrite.

---

## 6. Summary table (normative for README)

| Metric | Result (8-slot ring) |
|--------|----------------------|
| ICMP RTT avg | **~0.08 ms** |
| TCP goodput | **~3.7 Gbit/s** either direction |
| TCP retransmits / 10 s | **~5.5e4** (improved vs ~1.6e5 single-slot; not zero) |
| UDP goodput | **~3.5 Gbit/s** |
| UDP loss at unlimited offer | **~3.4%** |

---

## 7. Design limits still in play

| Limit | Impact |
|-------|--------|
| Ring depth 8 | Finite burst absorption; then `-ENOSPC` drops |
| `spin_lock_bh` + kthread | Serialization and scheduling lag |
| No `NETDEV_TX_BUSY` | Stack may keep offering until ring full drops |
| `netif_rx` | Softnet pressure under flood |

---

## 8. Re-measurement

Re-run after ring depth, locking, or RX path changes; update if results move **>15%**. Record `natactl status` ring_full_drops after iperf.

---

## 9. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [02-packet-format.md](02-packet-format.md)  
