# 08 — Performance Specification (Simulation)

**Spec version:** 0.4 (as-built design; remeasure pending for 32-slot + backpressure)  
**Status:** MEASURED baselines through 8-slot + NAPI (2026-07-15); **32-slot + `NETDEV_TX_BUSY` not yet re-benched in this environment** (no root for netns)  
**Mode:** In-kernel mailbox simulation only — **not** SATA PHY throughput  

---

## 1. Scope

```text
netdev TX → spin_lock_bh → ring enqueue → (stop queue if full) → napi_schedule(peer)
  → softirq NAPI poll → short lock dequeue → wake producer if stopped → napi_gro_receive
```

Numbers bound software encapsulation with a **32-slot ring per direction**, **per-netdev NAPI**, and **TX backpressure**. They are not SATA line rate.

---

## 2. Lab environment (last full bench)

| Item | Value |
|------|--------|
| Date | 2026-07-15 (post NAPI; pre 32-slot/backpressure) |
| Kernel | Linux 6.8.0-124-generic |
| CPU | AMD EPYC-Milan, 8 vCPU |
| Module (last measured) | `nata.ko` sim, **8×16-sector rings**, **NAPI RX** |
| Module (current tree) | **32×4-sector rings**, **NAPI RX**, **`NETDEV_TX_BUSY`** |
| Topology | netns `nata-a` ↔ `nata-b` |
| Tools | `ping`, `iperf3` 3.16 |

---

## 3. Latency (ICMP, 50 packets) — last measured (8-slot + NAPI)

| Metric | Value |
|--------|-------|
| Loss | **0%** |
| RTT min / avg / max | **0.035 / 0.051 / 0.068 ms** |

Single-packet latency remains well under a millisecond (same-host path). 32-slot geometry does not change the single-packet path length; re-check after load if desired.

---

## 4. TCP throughput (iperf3, 10 s)

### 4.1 Last measured (8-slot + NAPI)

| Direction | Bitrate | Retransmits (10 s) |
|-----------|---------|---------------------|
| A → B | **~4.42 Gbit/s** (sender = receiver) | **~57k** |
| B → A (`-R`) | **~4.79 Gbit/s** | **~53k** |

### 4.2 Evolution on this host (same day, through NAPI)

| Metric | Single-slot kthread | 8-slot + kthread | 8-slot + NAPI | 32-slot + BUSY |
|--------|---------------------|------------------|---------------|----------------|
| TCP goodput | ~0.60 Gbit/s | ~3.7 Gbit/s | **~4.4–4.8 Gbit/s** | **TBD remeasure** |
| TCP retransmits / 10 s | ~164k | ~55–58k | **~53–57k** | **TBD remeasure** |

NAPI raised goodput ~20% over the kthread drain. Depth 8 still underfilled TCP bursts (high retransmits + `ring_full_drops` as true drops). Current tree: depth **32** and **`NETDEV_TX_BUSY`** so the stack retries instead of dropping at the mailbox; expect lower retransmits and near-zero true packet drops from ring full. **Do not invent numbers** — re-run with root:

```bash
sudo ./scripts/nata-ns-up.sh
sudo ip netns exec nata-a ping -c 50 192.168.42.2
sudo ip netns exec nata-b iperf3 -s
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 -R
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -u -b 0 -t 10
./tools/natactl status   # ring_full_drops = BUSY hits, not drops
```

---

## 5. UDP (iperf3 unlimited, 10 s) — last measured (8-slot + NAPI)

| Side | Result |
|------|--------|
| Sender offered | **~2.49 Gbit/s** |
| Receiver goodput | **~2.49 Gbit/s** |
| Loss | **~0.073%** (~1.5k / 2.1M datagrams) |

### Evolution

| Metric | Single-slot | 8-slot kthread | 8-slot NAPI | 32-slot + BUSY |
|--------|-------------|----------------|-------------|----------------|
| UDP goodput | ~2.23 Gbit/s | ~3.54 Gbit/s | **~2.49 Gbit/s** | **TBD** |
| UDP loss (unlimited) | ~45% | ~3.4% | **~0.07%** | **TBD** |

After a full bench suite on 8-slot + NAPI, `natactl status` showed **`ring_full_drops` ~1.1e5** (then true drops). With backpressure, the same counter means **BUSY hits**; true mailbox drops should be ~0.

---

## 6. Summary table

| Metric | Result (8-slot + NAPI, last full lab) | Current tree (32-slot + BUSY) |
|--------|----------------------------------------|-------------------------------|
| ICMP RTT avg | **~0.05 ms** | expect similar; remeasure |
| TCP goodput | **~4.4–4.8 Gbit/s** either direction | remeasure |
| TCP retransmits / 10 s | **~5.5e4** | remeasure (expect lower) |
| UDP goodput (`-b 0`) | **~2.5 Gbit/s** | remeasure |
| UDP loss at unlimited offer | **~0.07%** | remeasure |
| Ring full handling | drop + count | **`NETDEV_TX_BUSY` + stop/wake** |

---

## 7. Design limits still in play

| Limit | Impact |
|-------|--------|
| Ring depth 32 | Finite burst absorption; then `NETDEV_TX_BUSY` until free |
| Shared `spin_lock` for both directions | TX and RX serialize briefly on enqueue/dequeue |
| Softirq NAPI budget (default 64) | Poll yields; may reschedule on residual pending |
| Slot size 2048 B | Fits `ETH_FRAME_LEN`; no jumbo frames |

**Removed vs prior:** dedicated RX kthreads; always-`NETDEV_TX_OK` drop-on-full; 8×8192-byte slots.

---

## 8. Re-measurement

Re-run after ring depth, locking, NAPI weight, or TX path changes; update if results move **>15%**. Record `natactl status` `ring_full_drops` after iperf (BUSY pressure, not drops).

**This change set:** root netns bench **not run** in the agent environment (`sudo` requires a password). Fill section 4/5 TBD cells before treating numbers as normative for the 32-slot path.

---

## 9. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [02-packet-format.md](02-packet-format.md)  
- [04-kernel-module.md](04-kernel-module.md)  
