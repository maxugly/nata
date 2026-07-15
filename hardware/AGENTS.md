# hardware/ — Physical Bridge Design Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Electromechanical design assets for a dual-target SATA bridge board (FPGA + two host-facing SATA connectors, power/ground strategy). Complements gateware in `firmware/` and the software stack; does **not** by itself prove link bring-up.

---

## Ownership

| Path | Owns |
|------|------|
| `schematics/` | Circuit intent: power, FPGA connectivity, SATA connectors, debug |
| `pcb/` | Layout: placement, high-speed routing, stackup-dependent decisions |

Format in-tree today: KiCad (`.kicad_sch`, `.kicad_pcb`).

---

## Local Contracts

- **Maturity: design assets / PARTIAL.** Presence of KiCad files ≠ validated hardware.
- **SATA data is AC-coupled.** Do not “fix” isolation by removing coupling strategy without an explicit electrical review note in docs.
- **Common ground** between bridge and hosts is lab practice; document intentional exceptions.
- **No experimental attachment to disks with data you care about** — bridge emulates devices; keep that warning in user-facing docs if bring-up instructions expand.
- Pinout and transceiver choices must stay compatible with `firmware/constraints/` examples or those examples must be updated when the board freezes a pin map.
- Do not commit large binary exports, personal fab account data, or manufacturer quote PDFs unless the project explicitly wants them versioned.

---

## Work Guidance

1. Electrical/protocol *why* may live in [../docs/whitepaper.md](../docs/whitepaper.md); board-specific BOM/pin freeze should be as-built notes (new doc under `docs/` or hardware README) when ready.
2. High-speed pair length matching and reference design rules stay with PCB child contract.
3. Coordinate connector/pin changes with firmware constraints and RTL top-level ports.
4. Prefer KiCad project hygiene: consistent sheet names, no floating power pins in schematic intent.

---

## Verification

- [ ] Schematic and PCB still refer to the same connectors/FPGA package intent
- [ ] Firmware constraint comments still name the correct example board if this design is that board
- [ ] Root README hardware section not overstating fab/validation status

---

## Child DOX Index

| Path | Owns |
|------|------|
| [schematics/AGENTS.md](schematics/AGENTS.md) | KiCad schematic |
| [pcb/AGENTS.md](pcb/AGENTS.md) | KiCad PCB layout |
