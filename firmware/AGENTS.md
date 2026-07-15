# firmware/ — FPGA Gateware Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

FPGA-facing design: dual SATA **device** endpoints, dual-port mailbox BRAM, pin constraints, smoke testbench. Current maturity is **PARTIAL / stub** — structural scaffolding for a dual-target bridge, not a verified SATA product.

---

## Ownership

| Path | Owns |
|------|------|
| `rtl/` | Synthesizable (intent) Verilog modules |
| `sim/` | Testbench and simulation-only harness |
| `constraints/` | Pin / clock / transceiver placement examples |

Normative software mailbox geometry is owned by the kernel + `docs/specs/03`; RTL must **match** that size and addressing story, not invent a second map silently.

---

## Local Contracts

- **Status default for this tree:** PARTIAL. Do not mark IMPLEMENTED for full SATA link without dual-host proof.
- **Mailbox capacity:** 128 KiB (32768 × 32-bit) unless a coordinated project-wide change updates module + specs + RTL together.
- **Two ports, one mailbox:** Port A and Port B share `dual_port_ram`; write events cross-wire for AN-style signaling.
- **PHY/SERDES:** blackbox / forced-ready patterns are stubs until real GTP/GTX integration lands — keep that obvious in code comments and `docs/specs/07`.
- **Gaps to production** live in [../docs/specs/07-fpga-rtl.md](../docs/specs/07-fpga-rtl.md). Extending RTL without updating that checklist is incomplete work.
- Constraints are **board-example** (Artix-7 / AC701-style); retargeting boards is expected — document pinfile purpose.

---

## Work Guidance

1. Read child `AGENTS.md` for the subdirectory you edit.
2. Prefer incremental, reviewable RTL; avoid giant unexplained FSM rewrites.
3. When adding real SERDES or FIS completeness, upgrade maturity tags only for what is tested.
4. Keep software packet format opaque to RTL (sector data); do not parse NATA headers in FPGA unless you also spec that.

---

## Verification

- [ ] Spec 07 reflects new ports, states, or BRAM geometry
- [ ] Mailbox depth/width still matches software 128 KiB / 512 B sectors
- [ ] Run structural smoke after RTL/TB changes: `make -C firmware/sim` (see `sim/AGENTS.md`)
- [ ] No README language claiming hardware bring-up complete

---

## Child DOX Index

| Path | Owns |
|------|------|
| [rtl/AGENTS.md](rtl/AGENTS.md) | Verilog RTL modules |
| [sim/AGENTS.md](sim/AGENTS.md) | Simulation testbench |
| [constraints/AGENTS.md](constraints/AGENTS.md) | XDC / pin constraints |
