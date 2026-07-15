# firmware/sim/ — RTL Simulation Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Simulation-only harnesses that exercise `nata_top` (and eventually lower modules) without hardware.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata_tb.v` | Clock/reset smoke, link LED check, forced write-event / activity path, VCD dump |
| `xilinx_sim_stubs.v` | Simulation-only models for Xilinx primitives (`IBUFDS`, …) — **not for synthesis** |
| `Makefile` | One-command smoke: `make` → iverilog + vvp |

---

## Local Contracts

- Testbenches and sim stubs are **not synthesizable** and must not be added to production synthesis file lists without review.
- Timescale and clock periods should match constraint intent (e.g. 200 MHz sys clock → 5 ns period) or document deliberate differences.
- Prefer self-checking `$display` PASS/FAIL + `$finish`; silent exit is a failed test. ERROR paths should `$finish` after printing.
- **Forcing internal signals** (e.g. `write_event_*`) is allowed for stub-level TB; check the dependent LED/wire **while the force is still held** (a released pulse will read back low). When real FSMs exist, replace forces with protocol-level stimulus.
- Keep TB aligned with top-level port names; top port renames must update this TB in the same change.
- Build artifacts `*.vvp` and `*.vcd` are gitignored — never commit them.

### What this smoke test covers / does not cover

| Covers (as-built) | Does **not** cover |
|-------------------|---------------------|
| RTL elaborates under iverilog with sim stubs | Real GTP/SERDES or OOB |
| Forced `phy_ready` → both link LEDs high | Full FIS / DATA stream |
| Forced `write_event_a_to_b` → `led_activity_a` | Multi-sector DMA, CRC, AN FIS to host |
| Structural compile of top + device IP + BRAM | Dual-host hardware |

---

## Work Guidance

1. Extend tests when you claim new RTL behavior (OOB, FIS, multi-sector).
2. Do not delete failing assertions to “green” a stub — mark expected-fail or fix RTL.
3. Run commands live under [How to run](#how-to-run); keep [../../docs/specs/07-fpga-rtl.md](../../docs/specs/07-fpga-rtl.md) §7.1 in sync if the command line changes.
4. New Xilinx primitives in RTL need matching entries in `xilinx_sim_stubs.v` (or the sim will fail to elaborate).

---

## How to run

**Prerequisite:** Icarus Verilog (`iverilog` + `vvp`). Example installs: `sudo apt install -y iverilog` or `brew install icarus-verilog`.

From `firmware/sim/`:

```bash
# One command
make

# Or explicit compile + run
iverilog -o nata_tb.vvp -g2012 \
  nata_tb.v xilinx_sim_stubs.v \
  ../rtl/nata_top.v ../rtl/sata_device_ip.v ../rtl/dual_port_ram.v
vvp nata_tb.vvp
```

**Expected output:** should print SUCCESS for link LEDs and peer interrupt propagation, then `$finish`.

Example:

```text
[TB] SUCCESS: Both SATA link PHYs report locked and aligned.
[TB] SUCCESS: Peer interrupt propagated successfully to Port B.
[TB] Simulation completed successfully.
```

**Waveforms:** the TB dumps `nata_tb.vcd` (`$dumpfile` / `$dumpvars`). View with:

```bash
gtkwave nata_tb.vcd
```

(`gtkwave` optional; not required for pass/fail.)

```bash
make clean   # removes nata_tb.vvp and nata_tb.vcd
```

---

## Verification

```bash
cd firmware/sim && make
```

- [x] TB compiles against current top (iverilog + `xilinx_sim_stubs.v`)
- [x] Structural smoke prints SUCCESS for link LEDs and AN wiring
- [ ] New behaviors beyond this smoke have at least one positive check

---

## Child DOX Index

None. Leaf.
