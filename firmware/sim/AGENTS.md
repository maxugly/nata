# firmware/sim/ — RTL Simulation Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Simulation-only harnesses that exercise `nata_top` (and eventually lower modules) without hardware.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata_tb.v` | Clock/reset smoke, link LED check, forced write-event / activity path |

---

## Local Contracts

- Testbenches are **not synthesizable** and must not be added to production synthesis file lists without review.
- Timescale and clock periods should match constraint intent (e.g. 200 MHz sys clock → 5 ns period) or document deliberate differences.
- Prefer self-checking `$display` PASS/FAIL + `$finish`; silent exit is a failed test.
- **Forcing internal signals** (e.g. `write_event_*`) is allowed for stub-level TB; when real FSMs exist, replace forces with protocol-level stimulus.
- Keep TB aligned with top-level port names; top port renames must update this TB in the same change.

---

## Work Guidance

1. Extend tests when you claim new RTL behavior (OOB, FIS, multi-sector).
2. Do not delete failing assertions to “green” a stub — mark expected-fail or fix RTL.
3. Document how to run the TB in spec 07 or a short comment header when a standard simulator command is chosen (iverilog/vvp, vivado xsim, etc.).

---

## Verification

- Run with the project’s chosen simulator when available; if none is installed, say so and at least lint port connections mentally against `../rtl/nata_top.v`.
- [ ] TB compiles against current top
- [ ] New behaviors have at least one positive check

---

## Child DOX Index

None. Leaf.
