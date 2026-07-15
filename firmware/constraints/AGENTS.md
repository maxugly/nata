# firmware/constraints/ — Pin Constraints Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

FPGA pin, clock, and transceiver placement constraints. Example target: Artix-7 style (AC701-oriented comments in-tree).

---

## Ownership

| File | Responsibility |
|------|----------------|
| `artix7_pins.xdc` | Sys clock, reset, LEDs, GTP channel LOC filters |

---

## Local Contracts

- Constraints are **board-specific**. Treat this file as a **template/example**, not universal truth for every Artix-7.
- Clock period must match the intended `sys_clk` (currently 5.000 ns → 200 MHz in-file).
- GTP `LOC` constraints that filter on cell names require those hierarchical names to exist in the elaborated design; if RTL has no `gtp_port_*` cells, the constraint is **forward-looking** — say so in comments when editing.
- Do not commit machine-local absolute paths or proprietary board secrets.
- New boards → new `.xdc` (or clearly named section) + note in spec 07 and parent firmware README/docs as needed.

---

## Work Guidance

1. When top-level ports rename, update XDC `get_ports` lists in the same change.
2. Keep IOSTANDARD explicit for every constrained port.
3. Prefer one board per file over a maze of dead constraints.

---

## Verification

- [ ] Every constrained port exists on `nata_top` (or documented as future)
- [ ] Spec 07 pin table still roughly accurate if ports/pins changed
- [ ] No constraints referencing deleted LED/clock names

---

## Child DOX Index

None. Leaf.
