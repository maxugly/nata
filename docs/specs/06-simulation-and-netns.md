# 06 — Simulation Topology & Network Namespace Spec

**Spec version:** 0.1 (as-built)  
**Status:** IMPLEMENTED  
**Sources:** `scripts/nata-ns-up.sh`, `scripts/nata-ns-down.sh`, `scripts/nata-up.sh`, README

---

## 1. Why namespaces are required

Both `nata0` and `nata1` register in the **same kernel** and initially the **same network namespace** (the namespace of the process that loaded the module, typically init/root).

If both IPv4 addresses are configured in that namespace:

- The kernel marks both addresses as **local**
- ARP/IP “to the peer” may short-circuit or never complete neighbor discovery as two hosts
- Module TX/RX counters can still advance on some paths, but **ping between the pair is not a valid same-host test**

**Solution:** move each netdev into its own network namespace so the stack treats them as separate hosts sharing only the in-kernel mailbox.

---

## 2. Canonical simulation topology

```text
+---------------- netns nata-a ----------------+
|  lo up                                       |
|  nata0  192.168.42.1/24  (MAC random)        |
|         default: peer 192.168.42.2            |
+----------------------|-----------------------+
                       |  (kernel mailbox)
+----------------------|-----------------------+
|  nata1  192.168.42.2/24  (MAC random)        |
|  lo up                                       |
+---------------- netns nata-b ----------------+
```

| Parameter | Default | Override env |
|-----------|---------|--------------|
| Netns A name | `nata-a` | `NATA_NS_A` |
| Netns B name | `nata-b` | `NATA_NS_B` |
| Address A | `192.168.42.1/24` | `NATA_ADDR_A` |
| Address B | `192.168.42.2/24` | `NATA_ADDR_B` |
| Ping target from A | `192.168.42.2` | `NATA_PEER_B` |
| Module mode | `target_ata_port=-1` | (fixed in scripts) |

---

## 3. Script: `scripts/nata-ns-up.sh`

### 3.1 Requirements

- Root (`id -u` == 0)
- Built `module/nata.ko` (or `./nata.ko` in CWD)
- `ip` (iproute2), `ping`, shell with `set -e`

### 3.2 Algorithm

1. **Cleanup**  
   - Move `nata0`/`nata1` back to root netns if present  
   - Delete netns `NATA_NS_A` / `NATA_NS_B`  
   - `rmmod nata` (ignore errors)
2. **Load** `insmod $MODULE target_ata_port=-1`
3. **Wait** up to ~2 s (20 × 0.1 s) for `/sys/class/net/nata0` and `nata1`
4. **Create** netns A and B
5. **Move** `nata0` → A, `nata1` → B
6. **Configure A:** lo up; flush/add `ADDR_A` on nata0; link up  
7. **Configure B:** lo up; flush/add `ADDR_B` on nata1; link up  
8. **Show** neighbor tables before ping  
9. **Ping** `ping -c 3 $PEER_B` from netns A  
10. **Show** neighbor tables after; `ip -s link` stats  

### 3.3 Success criteria

- Exit 0  
- Three ICMP echo replies (default)  
- ARP neighbor entries populated on both sides after ping  

### 3.4 Operator follow-ups printed

```text
ip netns exec nata-a bash
ip netns exec nata-b bash
sudo ./scripts/nata-ns-down.sh
```

---

## 4. Script: `scripts/nata-ns-down.sh`

1. Require root  
2. Move `nata0` from A → root (pid 1 netns via `netns 1`)  
3. Move `nata1` from B → root  
4. Delete netns A and B  
5. `rmmod nata`  

All move/delete/rmmod failures ignored with `|| true` where appropriate so teardown is best-effort.

---

## 5. Script: `scripts/nata-up.sh` (same-namespace)

**Purpose:** smoke-test module load and interface appearance **without** netns isolation.

| Step | Action |
|------|--------|
| 1 | `rmmod nata` (ignore fail) |
| 2 | `insmod` sim mode |
| 3 | sleep 1; check sysfs netdevs |
| 4 | Assign `192.168.42.1/24` to nata0, `.2/24` to nata1 in **root** netns |
| 5 | `ip link set` both up |

**Explicit limitation:** ping between the two IPs will not validate end-to-end host-to-host behavior. Prefer `nata-ns-up.sh` for that.

---

## 6. Manual equivalent (normative for automation)

```bash
sudo insmod module/nata.ko target_ata_port=-1
sudo ip netns add nata-a
sudo ip netns add nata-b
sudo ip link set nata0 netns nata-a
sudo ip link set nata1 netns nata-b
sudo ip netns exec nata-a ip addr add 192.168.42.1/24 dev nata0
sudo ip netns exec nata-a ip link set nata0 up
sudo ip netns exec nata-b ip addr add 192.168.42.2/24 dev nata1
sudo ip netns exec nata-b ip link set nata1 up
sudo ip netns exec nata-a ping -c 3 192.168.42.2
```

---

## 7. Recommended verification commands

| Check | Command |
|-------|---------|
| Kernel log | `dmesg \| grep -i nata` |
| Link in A | `ip netns exec nata-a ip link show nata0` |
| Neighbors | `ip netns exec nata-a ip neigh show dev nata0` |
| Latency | `ip netns exec nata-a ping -c 50 -i 0.2 192.168.42.2` |
| Bandwidth | `ip netns exec nata-b iperf3 -s` then client in A |
| Module stats | `natactl status` |

---

## 8. iperf3 / ping methodology (lab)

Documented measured numbers: [08-performance.md](08-performance.md).

Standard commands:

```bash
# Latency
sudo ip netns exec nata-a ping -c 50 -i 0.2 192.168.42.2

# TCP
sudo ip netns exec nata-b iperf3 -s
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 -R

# UDP unlimited offer
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -u -b 0 -t 10
```

---

## 9. Failure modes

| Symptom | Likely cause |
|---------|----------------|
| `nata.ko not found` | Build module first |
| Interfaces never appear | Init failure; check `dmesg` |
| Ping 100% loss | Wrong netns; addresses not up; module not sim |
| `Operation not permitted` on netns | Not root / missing CAP_SYS_ADMIN |
| rmmod: device busy | Netdevs still in netns; run down script or move links first |

---

## 10. Related

- [04-kernel-module.md](04-kernel-module.md)  
- [08-performance.md](08-performance.md)  
