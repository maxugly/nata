# 08 — Performance Specification (Simulation)

**Spec version:** 0.1 (as-built)  
**Status:** MEASURED (lab baseline 2026-07-15)  
**Mode:** In-kernel mailbox simulation only — **not** SATA PHY throughput  

---

## 1. Scope

These numbers characterize the **software path**:

```text
netdev TX → spinlock → memcpy mailbox → wake → kthread → memcpy → netif_rx
```

They bound encapsulation overhead and single-slot mailbox behavior. They **do not** represent:

- SATA Gen1 (1.5 Gbit/s) or Gen2 (3.0 Gbit/s) line rate  
- FIS/8b10b/protocol efficiency on wire  
- Dual-host CPU + interrupt overhead  

---

## 2. Lab environment (baseline)

| Item | Value |
|------|--------|
| Date | 2026-07-15 |
| Kernel | Linux 6.8.0-124-generic |
| CPU | AMD EPYC-Milan, 8 vCPU |
| Module | `nata.ko` simulation (`target_ata_port=-1`) |
| Topology | netns `nata-a` (`nata0` / 192.168.42.1) ↔ `nata-b` (`nata1` / 192.168.42.2) |
| Tools | `ping`, `iperf3` 3.16 |

---

## 3. Latency (ICMP)

### 3.1 Method

```bash
ip netns exec nata-a ping -c 50 -i 0.2 192.168.42.2
```

### 3.2 Results

| Metric | Value |
|--------|-------|
| Packets | 50 sent / 50 received |
| Loss | **0%** |
| RTT min | **0.072 ms** |
| RTT avg | **0.151 ms** |
| RTT max | **0.453 ms** |

### 3.3 Interpretation

Sub-millisecond RTT is expected for same-host memory copy + scheduling. Max spikes reflect scheduler preemption, not physical serialization delay.

---

## 4. TCP throughput (iperf3)

### 4.1 Method

```bash
# server
ip netns exec nata-b iperf3 -s
# client forward
ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 -i 2
# client reverse (server sends)
ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 -i 2 -R
```

### 4.2 Results

| Direction | Sender | Receiver | Transfer (10 s) | Notes |
|-----------|--------|----------|-----------------|-------|
| A → B | **~600 Mbit/s** | **~587 Mbit/s** | ~715 MiB | High TCP retransmits observed |
| B → A (`-R`) | **~609 Mbit/s** | **~608 Mbit/s** | ~726 MiB | Symmetric within noise |

### 4.3 Interval sample (A → B)

| Interval | Bitrate (approx.) |
|----------|-------------------|
| 0–2 s | 549 Mbit/s |
| 2–4 s | 586 Mbit/s |
| 4–6 s | 572 Mbit/s |
| 6–8 s | 662 Mbit/s |
| 8–10 s | 628 Mbit/s |

### 4.4 Retransmits

Order of **~1.6×10⁵** retransmits per 10 s stream was observed. Primary causes under current design:

1. **Single-slot mailbox** — publisher overwrites unconsumed frames  
2. No L2 flow control / TX queue stop on “busy” mailbox  
3. RX via dedicated kthread + `netif_rx` rather than high-rate NAPI  

TCP remains correct (reliable byte stream) but goodput is far below raw memory bandwidth.

---

## 5. UDP throughput (iperf3)

### 5.1 Method

```bash
ip netns exec nata-a iperf3 -c 192.168.42.2 -u -b 0 -t 10 -i 2
```

`-b 0` = unlimited offer rate.

### 5.2 Results

| Side | Bitrate | Notes |
|------|---------|-------|
| Sender offered | **~4.04 Gbit/s** | ~4.70 GiB in 10 s |
| Receiver goodput | **~2.23 Gbit/s** | ~2.60 GiB delivered |
| Loss | **~45%** of datagrams | Jitter ~0.002 ms at receiver |

### 5.3 Interpretation

UDP exposes the **drop/overwrite ceiling** without retransmission. Receiver goodput (~2.2 Gbit/s) is the useful “how hard can the path push one-way” figure under this host. Offered rate above that is discarded.

---

## 6. Summary table (normative for README)

| Metric | Result |
|--------|--------|
| ICMP RTT avg | **~0.15 ms** |
| TCP goodput (either direction) | **~0.6 Gbit/s** |
| UDP goodput (unlimited offer) | **~2.2 Gbit/s** |
| UDP loss at unlimited offer | **~45%** |

---

## 7. Theoretical vs measured (context)

| Bound | Approximate ceiling | Relation to NATA sim |
|-------|---------------------|----------------------|
| SATA Gen2 raw | 3.0 Gbit/s signaling | Hardware target, not sim |
| SATA Gen2 8b/10b payload | ~2.4 Gbit/s | Hardware target |
| Ethernet 1G | 1 Gbit/s | Sim TCP already near 0.6G; UDP goodput can exceed 1G on host |
| Host memory copy | Tens of Gbit/s | Not reached; software structure limited |

---

## 8. Performance-relevant design limits

| Limit | Spec impact |
|-------|-------------|
| One packet slot per direction | Hard cap under concurrent TX |
| `spin_lock_bh` around full frame copy | Serializes both directions somewhat |
| No TSO/GSO/checksum offload | Per-frame CPU cost |
| `netif_rx` | Softnet backlog pressure under flood |
| kthread wakeup per packet | Scheduling cost vs pure NAPI poll |

---

## 9. Re-measurement procedure

1. `sudo ./scripts/nata-ns-up.sh`  
2. Run §3 and §4 commands; capture full iperf3 summaries  
3. Record `uname -r`, CPU model, `nproc`, date  
4. Update this document and README table if results change by **>15%** or design changes  

Optional telemetry:

```bash
./tools/natactl status
ip netns exec nata-a ip -s link show nata0
```

---

## 10. Hardware performance (PLANNED)

No measured dual-host SATA numbers exist in-tree. When available, document separately:

- Link rate negotiated (1.5 / 3.0 Gbit/s)  
- iperf3 TCP/UDP host-to-host  
- CPU% and interrupt rate with AN  
- Comparison to this simulation baseline  

---

## 11. Related

- [02-packet-format.md](02-packet-format.md)  
- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [06-simulation-and-netns.md](06-simulation-and-netns.md)  
