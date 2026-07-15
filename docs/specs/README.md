# NATA Specification Index

**Project:** NATA (Not Advanced Technology Attachment)  
**Document set version:** 0.1 (as-built)  
**Status:** Living specification of *what the tree implements today*  
**Date baseline:** 2026-07-15  

These documents describe the **as-built** system: kernel simulation path, on-wire (on-mailbox) framing, control plane, userspace tooling, FPGA RTL stubs, and measured simulation performance. They are normative for the current software stack. Target hardware behavior that is not yet implemented is called out explicitly as **TBD / not implemented**.

| Doc | Title | Scope |
|-----|--------|--------|
| [01-system-overview.md](01-system-overview.md) | System overview | Goals, components, modes, maturity matrix |
| [02-packet-format.md](02-packet-format.md) | Packet format | Header, endianness, length rules, publication order |
| [03-mailbox-memory-map.md](03-mailbox-memory-map.md) | Mailbox memory map | 128 KiB layout, LBA ranges, dual-port model |
| [04-kernel-module.md](04-kernel-module.md) | Kernel module | Init, netdevs, TX/RX paths, threads, locking |
| [05-control-plane.md](05-control-plane.md) | Control plane | `/dev/nata_ctl`, ioctls, `natactl`, udev |
| [06-simulation-and-netns.md](06-simulation-and-netns.md) | Simulation & netns | Scripts, addressing, bring-up/tear-down |
| [07-fpga-rtl.md](07-fpga-rtl.md) | FPGA RTL | Top, dual-port RAM, SATA device IP stubs, constraints |
| [08-performance.md](08-performance.md) | Performance | Measured sim bandwidth/latency, methodology |

**Related (non-normative):** [../whitepaper.md](../whitepaper.md) — rationale and electrical/protocol motivation.

## Notation

| Tag | Meaning |
|-----|---------|
| **IMPLEMENTED** | Present in tree and exercised by sim path |
| **PARTIAL** | Structure/API present; behavior incomplete or stubbed |
| **PLANNED** | Documented intent; no working code path yet |
| **N/A** | Not applicable in current mode |

## Source of truth (code map)

| Concern | Primary sources |
|---------|-----------------|
| Shared types / ioctls | `module/nata.h` |
| Module lifecycle, threads, ioctl | `module/nata_main.c` |
| Netdev registration / xmit | `module/nata_net.c` |
| Mailbox I/O, TX/RX framing | `module/nata_blk.c` |
| Userspace control | `tools/natactl.c` |
| Netns bring-up | `scripts/nata-ns-up.sh`, `scripts/nata-ns-down.sh` |
| FPGA top / mailbox / device IP | `firmware/rtl/*.v` |
| Pin constraints (Artix-7 example) | `firmware/constraints/artix7_pins.xdc` |
| RTL smoke testbench | `firmware/sim/nata_tb.v` |
