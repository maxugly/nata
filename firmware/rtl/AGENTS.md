# firmware/rtl/ — RTL Modules Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Synthesizable Verilog for the NATA bridge core hierarchy.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata_top.v` | Top: clocks/reset, dual `sata_device_ip`, mailbox, LED wiring, (future) GTP hooks |
| `dual_port_ram.v` | 128 KiB true dual-port memory model |
| `sata_device_ip.v` | Simplified link/transport FSMs, FIS parse stubs, READ/WRITE/IDENTIFY/AN stubs |

---

## Local Contracts

- **Hierarchy:** `nata_top` instantiates one mailbox and two `sata_device_ip` instances (A/B).
- **Clock story:** comments describe 150 MHz SATA II link clock; top may still pass through 200 MHz IBUFDS — document reality, do not invent MMCM until it exists in code.
- **`phy_ready` forced high** and OOB mostly stubbed unless you replace that with real PHY control — leave explicit assigns/comments.
- **BRAM address:** sector-oriented (`lba * 128 + dword_index` style) must stay consistent with 512 B sectors.
- **Cross events:** `local_write_event` of one port feeds `peer_write_event` of the other via top-level wires.
- Prefer synchronous reset style already used (`rst_n` active-low in device IP; top active-high `sys_rst`).

### What “done” is not

Completing a state enum does not complete SATA. Treat multi-cycle TX simplification, missing CRC, missing full DATA FIS streaming, and missing real SERDES as open until tested.

---

## Work Guidance

1. Mirror changes into [../../docs/specs/07-fpga-rtl.md](../../docs/specs/07-fpga-rtl.md) (FSM tables, ports, gaps checklist).
2. If BRAM size changes → also `module/nata_blk.c`, `nata_main.c`, specs 03/04/07, README.
3. Keep primitives/constants in one place inside `sata_device_ip` unless refactoring for real IP integration.
4. No vendor encrypted netlists without a clear license note in this tree’s docs.

---

## Verification

- [ ] Instantiation ports still match between top and children
- [ ] Spec 07 FSM/port tables updated
- [ ] Sim testbench still builds against top port list (update `../sim` if ports change)

---

## Child DOX Index

None. Leaf.
