# hardware/pcb/ — PCB Layout Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Physical layout of the NATA bridge board: component placement, high-speed SATA/FPGA transceiver routing, power distribution, mechanical outline.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata_bridge.kicad_pcb` | Board layout |

---

## Local Contracts

- Layout must track schematic connectivity; do not “fix” DRC by deleting nets without schematic update.
- **SATA / transceiver pairs:** prioritize length match, reference plane continuity, and short connector breaks over pretty silkscreen.
- Keep serviceability in mind: programming headers, LEDs, reset accessible.
- Fab outputs (Gerbers, drill, pick-and-place) are build artifacts — commit only if the project deliberately versions a spin; otherwise generate locally.
- Board spins: name/version clearly when multiple revisions exist (filename or title block).

---

## Work Guidance

1. Impedance targets depend on stackup; record stackup assumptions in a board note or docs when freezing for fab.
2. Coordinate connector placement with dual-host cable access (two hosts, one bridge).
3. After major placement moves, re-check constraint pin suitability for the example FPGA package.

---

## Verification

- [ ] Netlist import from schematic is clean for the intended spin
- [ ] No unconnected SATA pair halves left as stubs without reason
- [ ] Docs do not claim a fabbed/validated board unless that spin is real

---

## Child DOX Index

None. Leaf.
