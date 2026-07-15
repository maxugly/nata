# 07 — FPGA RTL Specification (As-Built Stubs)

**Spec version:** 0.1 (as-built)  
**Status:** PARTIAL — structural design and simplified FSMs; **not** a production SATA device IP  
**Sources:** `firmware/rtl/nata_top.v`, `dual_port_ram.v`, `sata_device_ip.v`, `firmware/sim/nata_tb.v`, `firmware/constraints/artix7_pins.xdc`

---

## 1. Intent vs reality

| Intent (whitepaper) | As-built RTL |
|---------------------|--------------|
| Two real SATA device PHYs | Transceiver blackboxes; `phy_ready` forced `1` |
| Full link + transport layers | Simplified combinational FSMs; multi-cycle TX simplified |
| Dual-port mailbox 128 KiB | **Implemented** (`dual_port_ram`) |
| AN on peer write | Event wires + `T_SEND_AN` stub |
| IDENTIFY / READ / WRITE | State branches present; incomplete data path |
| Gateware drop-in for silicon | **Not verified** on hardware |

This document specifies **what is in the Verilog tree**, not a certified SATA compliance suite.

---

## 2. Hierarchy

```text
nata_top
├── IBUFDS (sys clock)
├── dual_port_ram mailbox_ram
├── sata_device_ip sata_port_a
└── sata_device_ip sata_port_b
```

---

## 3. Top-level ports (`nata_top`)

### 3.1 Clocks and reset

| Port | Dir | Description |
|------|-----|-------------|
| `sys_clk_p` / `sys_clk_n` | in | 200 MHz differential system clock |
| `sys_rst` | in | Active-**high** board reset |

Internal: `sys_rst_n = ~sys_rst`.  
Clock path: `IBUFDS` → `sys_clk_ibuf` → `clk_150mhz` (**currently wired as pass-through**; comment notes real design uses MMCM for 150 MHz SATA II link clock).

### 3.2 SATA differential pairs

| Port | Dir | Side |
|------|-----|------|
| `sata_a_rx_p/n`, `sata_a_tx_p/n` | in/out | Host A |
| `sata_b_rx_p/n`, `sata_b_tx_p/n` | in/out | Host B |

**Not connected** through real GTP instances in current RTL (placeholders / forced ready).

### 3.3 LEDs

| Port | Source |
|------|--------|
| `led_link_a_up` | `phy_ready_a` |
| `led_link_b_up` | `phy_ready_b` |
| `led_activity_a` | `write_event_a_to_b` |
| `led_activity_b` | `write_event_b_to_a` |

---

## 4. Dual-port RAM (`dual_port_ram`)

### 4.1 Interface

| Port A | Port B | Width |
|--------|--------|-------|
| `clk_a`, `we_a`, `addr_a[14:0]`, `din_a[31:0]`, `dout_a[31:0]` | same for B | 32-bit data, 15-bit address |

### 4.2 Geometry

| Parameter | Value |
|-----------|-------|
| Depth | 32768 words |
| Width | 32 bits |
| Capacity | 128 KiB |
| LBA coverage | 0–255 at 512 B/sector |

### 4.3 Timing model

- Synchronous read: `dout` updates on clock edge to `ram[addr]` (read-first vs write-first: write updates memory; `dout` gets `ram[addr]` after write assignment in same block — classic write-first inference pattern for many tools).
- Independent ports; no built-in collision logic.

---

## 5. SATA device IP stub (`sata_device_ip`)

### 5.1 Ports

| Group | Signals |
|-------|---------|
| Clock/reset | `clk` (nominal 150 MHz), `rst_n` |
| SERDES | `rx_data[31:0]`, `rx_charisk[3:0]`, `tx_data`, `tx_charisk` |
| OOB | `oob_reset_det`, `oob_init_tx`, `oob_wake_tx`, `phy_ready` |
| BRAM | `ram_we`, `ram_addr[14:0]`, `ram_wdata`, `ram_rdata` |
| AN / events | `peer_write_event`, `local_write_event` |

### 5.2 Link-layer primitives (DWORD constants)

| Name | Value | Role |
|------|-------|------|
| `PRIM_ALIGN` | `32'h7B7B7C7C` | ALIGN |
| `PRIM_SYNC` | `32'hB5B57C7C` | SYNC (idle TX default) |
| `PRIM_SOF` | `32'h35357C7C` | Start of frame |
| `PRIM_EOF` | `32'h85857C7C` | End of frame |
| `PRIM_XRDY` | `32'h4A4A7C7C` | Transmitter ready |
| `PRIM_RRDY` | `32'h4B4B7C7C` | Receiver ready |
| `PRIM_WTRM` | `32'h58587C7C` | Wait for termination |
| `PRIM_R_OK` | `32'h5C5C7C7C` | Reception OK |
| `PRIM_R_ERR` | `32'h5D5D7C7C` | Reception error |

`tx_charisk` default `4'b0001` (K character in low byte) for primitives.

### 5.3 Link FSM states

| State | Code | Behavior (simplified) |
|-------|------|------------------------|
| `L_RESET` | 0 | Wait `phy_ready` → IDLE |
| `L_IDLE` | 1 | Drive SYNC; SOF→RX_FIS; or transport needs TX → TX_REQ |
| `L_RX_FIS` | 2 | Drive RRDY; capture data DWs; EOF→IDLE |
| `L_TX_REQ` | 3 | Drive XRDY; RRDY→TX_DATA |
| `L_TX_DATA` | 4 | Drive SOF; immediately → TX_WTRM (**not full multi-DW payload stream**) |
| `L_TX_WTRM` | 5 | Drive WTRM; R_OK/R_ERR → IDLE |

### 5.4 Transport FSM states

| State | Code | Trigger / role |
|-------|------|----------------|
| `T_IDLE` | 0 | Enter PARSE on RX_FIS; or SEND_AN if peer event pending |
| `T_PARSE_FIS` | 1 | Expect FIS type `0x27` (Register H2D) |
| `T_CMD_IDENTIFY` | 2 | ATA cmd `0xEC` |
| `T_CMD_READ` | 3 | Cmd `0x25` (READ DMA EXT) or `0x20` (READ SECTORS) |
| `T_CMD_WRITE` | 4 | Cmd `0x35` / `0x30` |
| `T_SEND_STATUS` | 5 | Device-to-Host status FIS skeleton |
| `T_SEND_AN` | 6 | Async notification status skeleton |

### 5.5 FIS parsing (Register H2D)

When `T_PARSE_FIS`:

| Field | Source bits (as coded) |
|-------|------------------------|
| `command` | `fis_rx_buf[0][23:16]` |
| LBA low… | assembled from `fis_rx_buf[1..3]` into `lba_addr[47:0]` |
| `sector_count` | from `fis_rx_buf[3..4]` |

FIS type check: `fis_rx_buf[0][7:0] == 8'h27`.

### 5.6 Command actions (stub level)

**IDENTIFY (`0xEC`):** preload `fis_tx_buf` with D2H header `0x34` and ASCII-ish model fragments `"NATAEMULATOR"`-style dwords; length 6 DWs.

**READ:**  
`ram_addr <= lba_addr[14:0] * 128 + dma_counter[6:0]`; fill `fis_tx_buf[dma_counter]` from `ram_rdata` for `dma_counter < 128`.

**WRITE:** while `L_RX_FIS` and data (non-K), write `rx_data` into BRAM at LBA-mapped address; at `dma_counter == 127` pulse `local_write_event`.

**STATUS:** `fis_tx_buf[0] <= 32'h0050_0034` (type 34h, status 0x50 ready).

**AN:** `fis_tx_buf[0] <= 32'h0850_0034` (notification-related bit set in status word as coded).

### 5.7 OOB handling

On `oob_reset_det`: assert `oob_init_tx` and `oob_wake_tx` for one cycle path (held while reset det true). Stub only.

### 5.8 Peer write / AN pending

```text
peer_write_event → peer_event_pending = 1
T_SEND_AN completion → peer_event_pending = 0
```

Cross-wiring in top:

```text
Port A local_write_event → write_event_a_to_b → Port B peer_write_event
Port B local_write_event → write_event_b_to_a → Port A peer_write_event
```

---

## 6. Pin constraints (Artix-7 example)

File: `firmware/constraints/artix7_pins.xdc`  
Reference board comments: **AC701-style** pin names.

| Signal | Example package pin | Standard |
|--------|---------------------|----------|
| `sys_clk_p` | R3 | DIFF_SSTL15 |
| `sys_clk_n` | P3 | DIFF_SSTL15 |
| `sys_rst` | U14 | LVCMOS15 |
| `led_link_a_up` | T14 | LVCMOS15 |
| `led_link_b_up` | T15 | LVCMOS15 |
| `led_activity_a` | T16 | LVCMOS15 |
| `led_activity_b` | U16 | LVCMOS15 |

Clock: `create_clock -period 5.000` (200 MHz) on `sys_clk_p`.

GTP placement LOC constraints:

| Channel | Filter intent |
|---------|----------------|
| `GTPE2_CHANNEL_X0Y0` | `*gtp_port_a*` |
| `GTPE2_CHANNEL_X0Y1` | `*gtp_port_b*` |

**Note:** Current `nata_top` does not instantiate named `gtp_port_*` cells; constraints are forward-looking.

---

## 7. Testbench (`nata_tb`)

| Item | Spec |
|------|------|
| Timescale | 1ns / 1ps |
| Clock | 200 MHz differential (2.5 ns half-period) |
| Stimulus | Assert reset 20 ns; release; check both link LEDs high |
| AN path | `force uut.write_event_a_to_b = 1`; check `led_activity_a` **while force held** |
| VCD | `$dumpfile("nata_tb.vcd")` / `$dumpvars(0, nata_tb)` |
| Sim stubs | `firmware/sim/xilinx_sim_stubs.v` provides behavioral `IBUFDS` |
| Pass criteria | Print SUCCESS messages; `$finish` |

Does **not** drive full SATA OOB or FIS byte streams.

### 7.1 Running the simulation

**Tool:** Icarus Verilog — need `iverilog` and `vvp` on `PATH` (any host package manager; package may be named `iverilog` or `icarus-verilog`). Distro-agnostic check and install table: [firmware/sim/AGENTS.md](../../firmware/sim/AGENTS.md#prerequisite-distro-agnostic).

```bash
command -v iverilog && command -v vvp   # both required
cd firmware/sim
make
# equivalent:
iverilog -o nata_tb.vvp -g2012 \
  nata_tb.v xilinx_sim_stubs.v \
  ../rtl/nata_top.v ../rtl/sata_device_ip.v ../rtl/dual_port_ram.v
vvp nata_tb.vvp
```

**What this validates (as-built):** the RTL hierarchy elaborates; forced `phy_ready` lights both link LEDs; forced `write_event_a_to_b` drives `led_activity_a` (structural AN/activity wiring). Optional waves: `gtkwave nata_tb.vcd` (viewer is optional and also distro-packaged under various names).

**What this does not validate:** full FIS packet streams, DATA FIS DMA, OOB/COMRESET, real SERDES, multi-sector mailbox transfers, or host-side Asynchronous Notification compliance. Those need a separate future testbench.

Do not commit `*.vvp` / `*.vcd` (gitignored).

---

## 8. Alignment with software mailbox

| Software | RTL |
|----------|-----|
| 128 KiB mailbox | 128 KiB BRAM |
| 512 B sectors | `lba * 128` DWORDs |
| Half-map LBA 0 / 128 | Same geometry assumed; not enforced in RTL |
| NATA packet header | **Not parsed in RTL** — opaque sector data |
| Soft wake_up | `local_write_event` / `peer_write_event` / AN |

---

## 9. Gaps to production (checklist)

- [x] Basic structural simulation passing (iverilog)  
- [ ] Real GTP/GTX + OOB analog timing  
- [ ] Full link layer retries, credit, ALIGN insertion  
- [ ] Complete FIS data payload streaming for multi-sector  
- [ ] 48-bit LBA and NCQ (not required for MVP)  
- [ ] IDENTIFY DEVICE full 512-byte payload  
- [ ] SATA Asynchronous Notification per AHCI host expectations  
- [ ] CDC if ports use independent recovered clocks  
- [ ] Formal / compliance testing against analyzer  
- [ ] Protocol-level FIS testbench (beyond structural force/LED smoke)  

---

## 10. Related

- [03-mailbox-memory-map.md](03-mailbox-memory-map.md)  
- [../whitepaper.md](../whitepaper.md)  
