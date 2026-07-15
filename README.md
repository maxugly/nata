# NATA: Not Advanced Technology Attachment

**NATA** is a point-to-point networking stack that carries Ethernet/IP frames over a SATA link by presenting each host with a synthetic block device and mapping packets onto Logical Block Address (LBA) regions.

A dual-target FPGA bridge sits between two host SATA controllers. Each host believes it is talking to a drive. The bridge terminates Host-to-Device FIS traffic on both sides, emulates device responses, and exchanges payload through shared dual-port memory (the mailbox). A Linux kernel module registers virtual NICs (`nata0` / `nata1`), encapsulates frames into fixed-size block writes, and rehydrates frames on the receive path.

The result is a full L2/L3-capable interface whose physical medium is the storage bus rather than a conventional Ethernet PHY.

---

## Status

| Layer | State |
|-------|--------|
| **Linux simulation mode** | Implemented — dual virtual NICs share an in-kernel mailbox (`target_ata_port=-1`) |
| **Kernel module** | `module/` — net + block helpers, control device `/dev/nata_ctl` |
| **User tools** | `tools/natactl`, `scripts/nata-ns-up.sh` (recommended sim), `scripts/nata-up.sh` |
| **FPGA RTL** | `firmware/rtl/` — dual-port RAM, SATA device IP stubs, top-level |
| **PCB / schematic** | `hardware/` — KiCad bridge design |
| **Hardware bring-up** | In progress — dual SATA device PHY + Asynchronous Notification path |

Simulation mode is the supported development path today: no FPGA or second machine is required to exercise encapsulation, sequencing, and the virtual interfaces.

---

## Architecture

Two host AHCI controllers never speak Host-to-Host. The bridge appears as a SATA device on each port, so each side only ever exchanges legal Host↔Device Frame Information Structures. Packet data is written to and read from mailbox LBA ranges in dual-port BRAM (or a software stand-in of the same layout).

```text
+-------------------+              +-----------------------+              +-------------------+
|   Host PC A       |              |  NATA bridge          |              |   Host PC B       |
|  (Linux)          |              |  (FPGA dual-target)   |              |  (Linux)          |
|                   |              |                       |              |                   |
|  [ Virtual NIC ]  |              | +-------------------+ |              |  [ Virtual NIC ]  |
|         |         |              | |  Device core 1    | |              |         |         |
|  [ AHCI / libata ]|              | +---------+---------+ |              |  [ AHCI / libata ]|
|         |         |  SATA cable  |           |           |  SATA cable  |         |         |
|   (host mode)     +------------->|     [ SRAM mailbox ]  |<-------------+   (host mode)     |
|                   |   3.0 Gbps   |           |           |   3.0 Gbps   |                   |
|                   |              | +---------+---------+ |              |                   |
|                   |              | |  Device core 2    | |              |                   |
|                   |              | +-------------------+ |              |                   |
+-------------------+              +-----------------------+              +-------------------+
```

**Mailbox layout (simulation defaults):**

| Region | LBA range (approx.) | Role |
|--------|---------------------|------|
| Lower 64 KiB | 0–127 | TX for `nata1` / RX for `nata0` |
| Upper 64 KiB | 128–255 | TX for `nata0` / RX for `nata1` |

Each packet is prefixed with a small NATA header (`magic`, `len`, `seq`) and padded to 512-byte sectors. Transmit publishes payload then header (with memory barriers); receive validates length (`ETH_HLEN` … `ETH_FRAME_LEN`) and sequence before injecting into the network stack via `netif_rx`.

Further electrical and protocol rationale: [docs/whitepaper.md](docs/whitepaper.md).

---

## Properties

Relative to a conventional 1 GbE link on copper pair cabling:

* **Point-to-point collision domain** — SATA is a dedicated link between host and device. There is no shared CSMA/CD domain; collision rate on the wire is not a factor.
* **Full-duplex at link rate** — Each direction uses its own differential pair at SATA Gen1/Gen2 rates (up to 3.0 Gbps signaling on the PHY; effective payload rate is lower after FIS, sector, and software overhead).
* **Security through obscurity** — On the wire the medium is SATA FIS and block I/O, not Ethernet. Commodity packet sniffers do not attach to that path; use a SATA analyzer or capture on `nata0`/`nata1` at the host.

---

## Repository layout

```text
module/           Linux out-of-tree kernel module (nata.ko)
tools/            natactl userspace control utility
scripts/          Bring-up helpers (simulation)
firmware/rtl/     Verilog sources (dual-port RAM, device IP, top)
firmware/sim/     Testbench
firmware/constraints/  FPGA pin constraints (Artix-7 example)
hardware/         KiCad schematic and PCB for the bridge
docs/             Whitepaper
```

---

## Requirements

**Simulation (software only)**

* Linux kernel headers matching the running kernel
* GCC or Clang toolchain capable of building out-of-tree modules
* Root privileges to load the module and configure interfaces

**Hardware path (target)**

* FPGA with two high-speed serial transceivers suitable for SATA device PHYs (e.g. Artix-7 / Kintex-class)
* Two host systems with free SATA ports (AHCI)
* Dual-device bridge firmware and board (see `firmware/`, `hardware/`)

---

## Quick start (simulation)

### 1. Build the module

```bash
cd module
make
```

Produces `nata.ko` against `/lib/modules/$(uname -r)/build`.

### 2. Load and configure (network namespaces — required for same-host ping)

Both virtual NICs register in one kernel. If `nata0` and `nata1` stay in the
default network namespace with `192.168.42.1` and `192.168.42.2` assigned there,
the stack treats both addresses as **local**. Frames still cross the simulated
mailbox (module TX/RX counters climb), but ARP and IP do not complete between
“two hosts” that are really one. Split them with network namespaces:

```bash
sudo ./scripts/nata-ns-up.sh
```

That script:

1. Loads `nata.ko` in simulation mode (`target_ata_port=-1`)
2. Creates netns `nata-a` and `nata-b`
3. Moves `nata0` → `nata-a`, `nata1` → `nata-b`
4. Assigns `192.168.42.1/24` and `192.168.42.2/24`
5. Runs `ping -c 3` from `nata-a` to `192.168.42.2`

Manual equivalent:

```bash
sudo insmod module/nata.ko target_ata_port=-1
sudo ip netns add nata-a
sudo ip netns add nata-b
sudo ip link set nata0 netns nata-a
sudo ip link set nata1 netns nata-b
sudo ip netns exec nata-a ip addr add 192.168.42.1/24 dev nata0
sudo ip netns exec nata-a ip link set nata0 up
sudo ip netns exec nata-b ip addr add 192.168.42.2/24 dev nata1
sudo ip netns exec nata-b ip link set nata1 up
sudo ip netns exec nata-a ping -c 3 192.168.42.2
```

Override defaults if needed: `NATA_ADDR_A`, `NATA_ADDR_B`, `NATA_PEER_B`, `NATA_NS_A`, `NATA_NS_B`.

Same-namespace bring-up (`sudo ./scripts/nata-up.sh`) only loads the module and
configures both NICs in the root netns — useful for driver smoke tests, not for
end-to-end IP between the pair.

### 3. Verify

```bash
dmesg | grep -i nata
sudo ip netns exec nata-a ip link show nata0
sudo ip netns exec nata-a ip neigh show dev nata0
sudo ip netns exec nata-a ping -c 3 192.168.42.2
```

Example kernel log:

```text
nata: Initializing Software-Defined Simulation Environment...
nata: Operating in 100% Virtual Loopback Mode (no hardware required).
NATA: Registered virtual interfaces nata0 and nata1 successfully.
```

### 4. Benchmark (simulation)

With namespaces up (`nata-a` = `192.168.42.1`, `nata-b` = `192.168.42.2`):

```bash
# Latency
sudo ip netns exec nata-a ping -c 50 -i 0.2 192.168.42.2

# Bandwidth (server in nata-b, client in nata-a)
sudo ip netns exec nata-b iperf3 -s
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 -R   # reverse
sudo ip netns exec nata-a iperf3 -c 192.168.42.2 -u -b 0 -t 10
```

**Measured results** (2026-07-15, Linux 6.8.0, AMD EPYC-Milan, 8 vCPU; in-kernel mailbox simulation — not SATA hardware):

| Metric | Direction | Result |
|--------|-----------|--------|
| **ICMP RTT** | `nata-a` → `nata-b` | min **0.072 ms** / avg **0.151 ms** / max **0.453 ms** (50 pkts, 0% loss) |
| **TCP throughput** | `nata-a` → `nata-b` | **~600 Mbit/s** sender / **~587 Mbit/s** receiver (10 s) |
| **TCP throughput** | `nata-b` → `nata-a` (iperf3 `-R`) | **~609 Mbit/s** sender / **~608 Mbit/s** receiver (10 s) |
| **UDP goodput** | `nata-a` → `nata-b` | **~2.23 Gbit/s** receiver (**~4.04 Gbit/s** offered; ~45% loss at unlimited rate) |

These numbers reflect software encapsulation and the shared mailbox path on one host. Hardware SATA Gen1/Gen2 PHY rates and FIS overhead will differ; re-measure on real bridge silicon.

### 5. Unload

```bash
sudo ./scripts/nata-ns-down.sh
```

Or manually: move interfaces back to the root netns, delete the namespaces, then
`rmmod nata`.

---

## Hardware deployment

### Bridge

1. Program an FPGA board with dual concurrent SATA **device** PHY cores and the NATA mailbox logic (`firmware/`).
2. Connect **Host A** to bridge port 1 with a standard 7-pin SATA data cable.
3. Connect **Host B** to bridge port 2 the same way.
4. Power and ground the bridge according to the board design (`hardware/`). Hosts may use independent PSUs.

### Power and grounding notes

SATA data pairs are **AC-coupled**. There is no intentional DC path through the high-speed pairs between host and device, which is why host-to-host data wiring does not create a supply short through the cable.

Still observe ordinary lab practice:

* Provide a solid **common ground** between the bridge board and each host chassis (or a single ground reference for the bench) so shield and logic reference potentials stay controlled.
* Do **not** rely on “floating” or deliberately ungrounded supplies as a design feature.
* Use only the I/O and PHY supply rails specified by the FPGA board design.
* Keep SATA data cables short and undamaged; treat the bridge as a high-speed digital system, not a passive crossover.

Electrical and FIS-level motivation for the middleman (why a direct host–host cable fails link init) is documented in [docs/whitepaper.md](docs/whitepaper.md).

### Host software (hardware path)

When device binding is implemented, the same module will attach to a real target rather than the in-memory mailbox. Until then, use simulation mode for stack development. Control ioctls are exposed on `/dev/nata_ctl` (`NATA_IOC_STATUS`, bind/unbind placeholders).

---

## Control interface

| Item | Description |
|------|-------------|
| Module param `target_ata_port` | `-1` = simulation (default development mode) |
| `/dev/nata_ctl` | Misc device for status / future bind operations |
| `tools/natactl` | Userspace helper (build with `make -C tools`) |

---

## FAQ

**Why not use Ethernet?**  
NATA is for environments where the only free high-speed interconnect is a SATA port, or where the research goal is storage-bus packet transport. It is complementary to Ethernet, not a general replacement for datacenter NICs.

**What is the effective throughput?**  
The SATA PHY may run at 3.0 Gbps. Usable bandwidth depends on sector size, FIS overhead, interrupt/notification path, and CPU cost of encapsulation. In **simulation** (same-host mailbox), expect roughly **~0.6 Gbit/s TCP** and **~2 Gbit/s UDP goodput** with sub-millisecond RTT — see [Benchmark (simulation)](#4-benchmark-simulation). Re-measure with `iperf3` / `ping` on real hardware once the bridge path is live.

**Can I boot over NATA?**  
Not supported. The module assumes a running kernel and registers virtual netdevs; it is not an iSCSI target or PXE path.

**Is capture with Wireshark possible?**  
On the virtual interfaces, yes — `nata0`/`nata1` are normal Linux netdevs. On the physical SATA cable, ordinary Ethernet sniffers do not apply; specialized SATA analyzers would see block traffic.

**Is this safe for production storage controllers?**  
Treat it as experimental. Simulation mode never touches real disks. Hardware mode requires a dedicated bridge that emulates devices; do not point experimental firmware at disks holding data you care about.

---

## Contributing

1. Prefer simulation-mode tests for kernel changes.
2. Keep hot-path bounds checks and sequence consumption intact (invalid frames must not busy-loop RX threads).
3. Do not commit Kbuild products (`*.o`, `*.ko`, `*.cmd`, `Module.symvers`, etc.); see `.gitignore`.
4. Document hardware or RTL changes alongside software when the mailbox layout or header format shifts.

---

## License

Licensed under the **NATA License**: You get *nothing*, it guarantees *nothing*.
