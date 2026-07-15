# 01 — System Overview

**Spec version:** 0.2 (as-built)  
**Status:** IMPLEMENTED (simulation); PARTIAL (FPGA/hardware)

---

## 1. Purpose

NATA is a **point-to-point networking stack** that carries **Ethernet frames** (and thus IP and higher protocols) by mapping packets onto **fixed-size block sectors** in a shared mailbox. The long-term physical medium is a **SATA link** terminated by a dual-target FPGA bridge so each host sees a legitimate SATA **device**. The short-term development medium is an **in-kernel simulated mailbox** shared by two virtual NICs on one host.

NATA does **not** implement:

- A filesystem or block device for general storage use
- Bridging or multi-peer switching (exactly two endpoints)
- Boot / PXE / iSCSI
- Encryption, authentication, or congestion control beyond what TCP provides above it

---

## 2. Design goals (current)

| ID | Goal | Realization today |
|----|------|-------------------|
| G1 | Full L2 Ethernet netdev semantics | Two `struct net_device` (`nata0`, `nata1`) with random MACs, carrier on |
| G2 | L3 usable on same host for development | Separate network namespaces (`nata-a`, `nata-b`) |
| G3 | Sector-aligned encapsulation | 16-byte NATA header + frame, padded to 512 B sectors |
| G4 | Safe concurrent mailbox access | Single spinlock + `smp_wmb`/`smp_rmb` publication order |
| G5 | No busy-spin on bad frames | Invalid slots clear `valid` and advance tail; NAPI poll does not spin on poison |
| G6 | Dual-host SATA path | **PLANNED** — RTL stubs + whitepaper only |

---

## 3. Component inventory

```text
+---------------------------+     +---------------------------+
|  Host stack A (or netns)  |     |  Host stack B (or netns)  |
|  apps / IP / ARP          |     |  apps / IP / ARP          |
|         |                 |     |         |                 |
|      nata0                |     |      nata1                |
|         |                 |     |         |                 |
+---------|-----------------+     +---------|-----------------+
          |                                 |
          +---------- nata.ko --------------+
                     |  sim_mailbox 128 KiB |
                     |  (or future block I/O)|
                     +----------------------+
                              |
              [ future: dual-port BRAM on FPGA ]
              [ future: SATA device PHY A/B   ]
```

| Layer | Artifact | State |
|-------|----------|--------|
| Kernel module | `module/nata.ko` (`nata_main`, `nata_net`, `nata_blk`) | IMPLEMENTED (sim) |
| Control device | `/dev/nata_ctl` misc device | IMPLEMENTED (status); bind **PARTIAL** |
| Userspace tool | `tools/natactl` | IMPLEMENTED |
| Udev rule | `module/99-nata.rules` | IMPLEMENTED |
| Netns scripts | `scripts/nata-ns-*.sh`, `nata-up.sh` | IMPLEMENTED |
| FPGA RTL | `firmware/rtl/` | PARTIAL (structural / stub) |
| Constraints | `firmware/constraints/artix7_pins.xdc` | PARTIAL (example pins) |
| Hardware CAD | `hardware/` KiCad | PARTIAL (design assets) |

---

## 4. Operating modes

### 4.1 Simulation mode (default, IMPLEMENTED)

- Module parameter: `target_ata_port=-1` (default and only fully supported path)
- Allocates `sim_mailbox` via `vmalloc(131072)`
- Both `nata0` and `nata1` register in the **loading** network namespace
- Packet path never touches AHCI, libata, or real disks
- Peer RX via `napi_schedule` on the peer NAPI (software stand-in for SATA Asynchronous Notification)

### 4.2 Hardware bind mode (PLANNED / API placeholders)

- Intended: bind module to a real block device / ATA port representing the FPGA mailbox
- `NATA_IOC_BIND` / `NATA_IOC_UNBIND` return `-EOPNOTSUPP` when `target_ata_port == -1`, else `-EINVAL`
- No live SCSI WRITE/READ path exists in the current tree

---

## 5. Endpoint model

| Role | Virtual NIC | Default netns (sim) | Default IPv4 | TX mailbox region | RX mailbox region |
|------|-------------|---------------------|--------------|-------------------|-------------------|
| Side A | `nata0` | `nata-a` | `192.168.42.1/24` | LBA 128–255 (upper 64 KiB) | LBA 0–127 (lower 64 KiB) |
| Side B | `nata1` | `nata-b` | `192.168.42.2/24` | LBA 0–127 (lower 64 KiB) | LBA 128–255 (upper 64 KiB) |

**Invariant:** Side A’s TX region is Side B’s RX region and vice versa. Each direction is a **32-slot ring** (`NETDEV_TX_BUSY` when full; no overwrite of published slots).

---

## 6. Protocol stack placement

```text
 Application
     |
   TCP/UDP/...          (unmodified)
     |
   IPv4/IPv6            (unmodified)
     |
   Ethernet L2          (unmodified frames on the netdev)
     |
   NATA encapsulation   (header + sector pad)   <-- this project
     |
   Mailbox sectors      (sim: memcpy; future: SATA FIS + DMA)
```

NATA is **not** a TUN/TAP userspace tunnel. Encapsulation runs in the kernel `ndo_start_xmit` path and decapsulation in per-netdev NAPI poll (`napi_gro_receive`).

---

## 7. Maturity matrix (detail)

| Capability | Sim | Hardware |
|------------|-----|----------|
| Register `nata0`/`nata1` | Yes | Same module (future bind) |
| Carrier on at register | Yes | TBD |
| TX encapsulate + publish | Yes | TBD |
| RX NAPI poll + inject | Yes (`napi_gro_receive`) | TBD (AN → `napi_schedule`) |
| ARP / ICMP / TCP / UDP | Yes (with netns) | TBD |
| Sequence numbers | Yes | Same format intended |
| Multi-packet ring queue | **Yes** (32 slots/dir in sim) | **No** on FPGA yet |
| Flow control / backpressure | **Yes** (`NETDEV_TX_BUSY` + stop/wake queue) | TBD |
| IDENTIFY DEVICE (ATA) | N/A | Stub in RTL |
| READ/WRITE DMA EXT | N/A | Stub state machine in RTL |
| Asynchronous Notification | Software `napi_schedule` | Stub `T_SEND_AN` in RTL |
| Real GTP/GTX SERDES | N/A | Blackbox / forced ready |
| Dual-host bring-up | N/A | Not demonstrated |

---

## 8. Versioning

| Field | Value |
|-------|--------|
| Module version string | `1.0` (`MODULE_VERSION`) |
| Spec document set | `0.1` |
| Packet magic | `0x4E415441` (`"NATA"`) |
| License (module) | GPL (`MODULE_LICENSE("GPL")`) |
| Project license text | NATA License (repo `LICENSE`) |

Module and userspace share `module/nata.h` for ioctl layouts; any ABI change to `struct nata_ioc_*` or `struct nata_pkt_hdr` requires a coordinated version bump in this document set.

---

## 9. Non-goals and known limitations

1. **Finite ring depth (32)** — under flood, TX returns `NETDEV_TX_BUSY` and stops the queue instead of overwriting; still not infinite buffering or credit-based flow control.
2. **Same-host default netns** — assigning both IPs in one namespace fails ARP/IP locality checks; netns isolation is required for end-to-end IP tests.
3. **TCP retransmits under extreme load** may still occur; deeper rings + backpressure cut them vs depth-8 drop path — see [08-performance.md](08-performance.md).
4. **No MTU customization** — standard Ethernet frame bounds enforced on RX (`ETH_HLEN` … `ETH_FRAME_LEN`); default netdev MTU from `alloc_etherdev`.
5. **Hardware path is not production-ready** — RTL is architectural scaffolding, not a verified SATA device IP.

---

## 10. Document dependencies

Read next: [02-packet-format.md](02-packet-format.md), [03-mailbox-memory-map.md](03-mailbox-memory-map.md).
