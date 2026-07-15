# hardware/schematics/ — Schematic Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Circuit-level definition of the NATA bridge: power rails, FPGA, dual SATA host connectors, programming/debug, LEDs/reset as applicable.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata_bridge.kicad_sch` | Primary schematic |

---

## Local Contracts

- Schematic is **source of connectivity intent**. PCB must implement it; if they diverge, fix one and note the freeze.
- Label differential SATA pairs clearly (TX/RX per port, polarity).
- Show AC-coupling capacitors on SATA data lines per design choice; do not leave coupling ambiguous.
- Power: document rail names and intended voltages on the sheet; no silent dual-purpose nets.
- ERC warnings that are accepted must be intentional (document in sheet notes if non-obvious).

---

## Work Guidance

1. Connector pin numbering must match the chosen SATA vertical/horizontal connector footprint used in PCB.
2. Any FPGA ball change → update `firmware/constraints` example and parent hardware docs.
3. Keep reference designators stable once a board spin is shared; use spin notes for renumbering.

---

## Verification

- [ ] KiCad schematic opens without missing libraries (or libraries are documented)
- [ ] Dual SATA ports both present in intent
- [ ] Parent hardware maturity language still accurate

---

## Child DOX Index

None. Leaf.
