# 08 — Performance Specification (Simulation)

**Spec version:** 0.3 (as-built)  
**Status:** MEASURED (lab baseline re-run 2026-07-15 after NAPI RX)  
**Mode:** In-kernel mailbox simulation only — **not** SATA PHY throughput  

---

## 1. Scope

```text
netdev TX → spinlock → ring enqueue → napi_schedule(peer)
  → softirq NAPI poll → short lock dequeue → napi_gro_receive
```

Numbers bound software encapsulation with an **8-slot ring per direction** and **per-netdev NAPI**. They are not SATA line rate.

---

## 2. Lab environment

| Item | Value |
|------|--------|
| Date | 2026-07-15 (post NAPI) |
| Kernel | Linux 6.8.0-124-generic |
| CPU | AMD EPYC-Milan, 8 vCPU |
| Module | `nata.ko` sim, **8×16-sector rings**, **NAPI RX** |
| Topology | netns `nata-a` ↔ `nata-b` |
| Tools | `ping`, `iperf3` 3.16 |

---

## 3. Latency (ICMP, 50 packets)

| Metric | Value |
|--------|-------|
| Loss | **0%** |
| RTT min / avg / max | **0.035 / 0.051 / 0.068 ms** |

Single-packet latency remains well under a millisecond (same-host path). Slightly better than the kthread path (~0.08 ms avg).

---

## 4. TCP throughput (iperf3, 10 s)

| Direction | Bitrate | Retransmits (10 s) |
|-----------|---------|---------------------|
| A → B | **~4.42 Gbit/s** (sender = receiver) | **~57k** |
| B → A (`-R`) | **~4.79 Gbit/s** | **~53k** |

### Evolution on this host (same day)

| Metric | Single-slot kthread | 8-slot + kthread | 8-slot + NAPI |
|--------|---------------------|------------------|---------------|
| TCP goodput | ~0.60 Gbit/s | ~3.7 Gbit/s | **~4.4–4.8 Gbit/s** |
| TCP retransmits / 10 s | ~164k | ~55–58k | **~53–57k** |

NAPI raised goodput ~20% over the kthread drain by injecting in softirq and releasing the ring lock before stack inject. Retransmits stay in the same order of magnitude: **depth 8 still underfills TCP bursts** under flood. Further cuts need deeper rings and/or `NETDEV_TX_BUSY` backpressure.

---

## 5. UDP (iperf3 unlimited, 10 s)

| Side | Result |
|------|--------|
| Sender offered | **~2.49 Gbit/s** |
| Receiver goodput | **~2.49 Gbit/s** |
| Loss | **~0.073%** (~1.5k / 2.1M datagrams) |

### Evolution

| Metric | Single-slot | 8-slot kthread | 8-slot NAPI |
|--------|-------------|----------------|-------------|
| UDP goodput | ~2.23 Gbit/s | ~3.54 Gbit/s | **~2.49 Gbit/s** |
| UDP loss (unlimited) | ~45% | ~3.4% | **~0.07%** |

NAPI nearly eliminates UDP loss; offered rate under `-b 0` is lower than the kthread run (CPU/scheduling mix), so goodput is not a pure apples-to-apples win. Prefer loss% and TCP goodput as primary trend lines.

After a full bench suite, `natactl status` showed **`ring_full_drops` ~1.1e5** — ring still saturates under flood.

---

## 6. Summary table (normative for README)

| Metric | Result (8-slot + NAPI) |
|--------|------------------------|
| ICMP RTT avg | **~0.05 ms** |
| TCP goodput | **~4.4–4.8 Gbit/s** either direction |
| TCP retransmits / 10 s | **~5.5e4** (similar to kthread ring; not zero) |
| UDP goodput (`-b 0`) | **~2.5 Gbit/s** |
| UDP loss at unlimited offer | **~0.07%** |

---

## 7. Design limits still in play

| Limit | Impact |
|-------|--------|
| Ring depth 8 | Finite burst absorption; then `-ENOSPC` / `ring_full_drops` |
| Shared `spin_lock` for both directions | TX and RX serialize briefly on enqueue/dequeue |
| No `NETDEV_TX_BUSY` | Stack may keep offering until ring-full drops |
| Softirq NAPI budget (default 64) | Poll yields; may reschedule on residual pending |

**Removed vs prior:** dedicated RX kthreads and `netif_rx` under `spin_lock_bh` for the full drain.

---

## 8. Re-measurement

Re-run after ring depth, locking, NAPI weight, or TX path changes; update if results move **>15%**. Record `natactl status` `ring_full_drops` after iperf.

---

## 9. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [02-packet-format.md](02-packet-format.md)  
- [04-kernel-module.md](04-kernel-module.md)  
