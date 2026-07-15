# NATA — Agent Contract (DOX)

This file is a **binding work contract** for agents and humans editing this repository.
It is not a README. It is not marketing. It is how work is done here.

DOX = durable, operational, executable documentation hierarchy. Parent rules apply repo-wide; child `AGENTS.md` files own their subtrees. Closer docs win on local detail; no child may weaken this contract.

---

## Core Contract

1. **As-built, not aspirational.** Describe what the tree *does*, not what someone wishes it did. Wishful design belongs in the whitepaper or a clearly marked PLANNED section — never mixed into normative “how it works” text without a tag.
2. **Nothing is oversold.** Gaps are named: `IMPLEMENTED`, `PARTIAL`, `PLANNED`, `TBD`, `stub`, `N/A`. If the FPGA path is a stub, say stub. If bind returns `-EOPNOTSUPP`, document that return.
3. **Ugly numbers stay in the table.** Retransmits, loss rates, single-slot overwrite, CPU cost — put them in specs with a short causal explanation. Do not bury failure modes in footnotes or omit them to look good.
4. **Confusing code gets a paragraph.** Historical names, inverted flags, surprising defaults (`is_dev0`, CLI LBA defaults vs kernel map) are first-class doc content. Save the next contributor three hours of debugger rage.
5. **Cross-document consistency is mandatory.** One mailbox, one packet header, one LBA map — shared across protocol, memory map, kernel, RTL, and performance docs. If you change one number, fix every dependent doc in the same change.
6. **Gaps to production are checklists.** Unfinished work ends in explicit bullet lists of what someone must still build — not vague “future work” paragraphs.
7. **Code is the source of truth.** Specs are derived from sources listed in [docs/specs/README.md](docs/specs/README.md). If code and doc disagree, fix the doc or fix the code; do not leave the drift.

---

## Read Before Editing

1. Read this root `AGENTS.md`.
2. List every path you expect to touch.
3. Walk from repo root to each path; read every `AGENTS.md` on the route.
4. For behavior changes under `module/`, `tools/`, `scripts/`, or `firmware/`, also open the relevant **as-built specs** under `docs/specs/` (see Child DOX Index).
5. Do not rely on memory from a prior session. Re-read the chain for this task.

---

## Update After Editing

Every meaningful change requires a **DOX pass** before the task is done.

| Change type | Update |
|-------------|--------|
| Packet header, magic, len/seq rules | `docs/specs/02-packet-format.md` (+ any code comments that restate them) |
| Mailbox size, LBA split, dual-port geometry | `03-mailbox-memory-map.md`, `07-fpga-rtl.md`, README mailbox table |
| TX/RX path, threads, locking, netdev ops | `04-kernel-module.md` |
| Ioctls, `/dev/nata_ctl`, `natactl` | `05-control-plane.md` |
| Netns scripts, default IPs, bring-up | `06-simulation-and-netns.md`, README quick start |
| RTL ports, FSMs, BRAM, constraints | `07-fpga-rtl.md` |
| Measured throughput/latency | `08-performance.md`, README benchmark section |
| Maturity of a whole layer | `01-system-overview.md`, `docs/specs/README.md` status tags |
| New durable subtree with its own rules | Add child `AGENTS.md` + index entry here |

Also update parent docs when structure, ownership, or the Child DOX Index changes. Remove stale or contradictory text immediately. Trivial typos that do not change contracts may skip a full pass; behavior or ABI changes may not.

---

## Project Map (where truth lives)

| Area | Path | Notes |
|------|------|--------|
| Kernel module | `module/` | Only fully working datapath today is **simulation** (`target_ata_port=-1`) |
| Userspace control | `tools/natactl.c` | Shares `module/nata.h` ioctl layouts |
| Bring-up | `scripts/nata-ns-*.sh` | Prefer netns for end-to-end IP; same-ns is smoke only |
| As-built specs | `docs/specs/` | Normative for current behavior — see child contract |
| Rationale / physics story | `docs/whitepaper.md` | Non-normative; may lag code |
| FPGA scaffolding | `firmware/rtl/` | PARTIAL / stub — do not claim hardware bring-up |
| Hardware CAD | `hardware/` | Design assets; not a verified product |

**Do not commit** Kbuild products (`*.o`, `*.ko`, `*.mod.c`, `Module.symvers`, `modules.order`, `.*.cmd`) or built `tools/natactl` binaries. See `.gitignore`.

---

## Work Guidance

### Simulation first

- Prefer `scripts/nata-ns-up.sh` for anything that claims “ping works” or measures bandwidth.
- Same-namespace `nata-up.sh` is for driver smoke tests only; do not use it to “prove” L3.
- Hot-path bounds checks and sequence consumption on bad frames must stay intact (no busy-loop on poisoned mailbox slots).

### Code change discipline

- Match existing style in the file you edit; no drive-by refactors.
- Kernel: keep `spin_lock_bh` + `smp_wmb`/`smp_rmb` publication order correct if you touch TX/RX.
- Userspace and kernel ioctl structs must stay in lockstep (`nata.h` is shared by include path).
- When you discover a footgun (bad name, inverted flag, silent drop), document it in the nearest as-built spec — not only in the PR description.

### Documentation discipline (the bar that got the good review)

These specs are a **contract**, not a brochure:

- Every normative spec file states **as-built** (and version/status) at the top.
- Status tags on capabilities: `IMPLEMENTED` / `PARTIAL` / `PLANNED` / `TBD` / `stub` / `N/A`.
- Put **ugly metrics** in tables with causes (e.g. ~1.6e5 TCP retransmits / 10s, ~45% UDP loss at unlimited offer, single-slot overwrite under load).
- Explain **confusing parts** in dedicated short sections (historical naming, defaults that disagree across layers).
- End incomplete subsystems with a **Gaps to production** (or equivalent) checklist of concrete missing items.
- Keep **cross-doc identity**: one mailbox size (128 KiB), one sector size (512), one LBA split (0–127 / 128–255), one header layout — four documents, one mailbox.

### What not to do

- Do not write “full SATA networking works” unless dual-host hardware is actually demonstrated and measured.
- Do not hide retransmit/loss numbers after a “successful” benchmark.
- Do not expand scope into boot/iSCSI/filesystem/multi-peer switching without an explicit human request.
- Do not “fix” hardware by deleting gap checklists.
- Do not leave specs claiming behavior that only exists in the whitepaper.

---

## Verification

When the change touches a layer, run what exists:

| Layer | Check |
|-------|--------|
| Module build | `make -C module` |
| Tool build | `make -C tools` |
| Sim bring-up | `sudo ./scripts/nata-ns-up.sh` (root / CAP_SYS_ADMIN) |
| Latency | `ip netns exec nata-a ping -c 50 …` |
| Bandwidth | `iperf3` server in `nata-b`, client in `nata-a` — update `08-performance.md` if results shift **>15%** or design changes |
| Control | `./tools/natactl status` with module loaded |
| Docs | Spec numbers match code constants (`NATA_MAGIC`, mailbox size, LBA bases, ioctl structs) |

If you cannot run root netns tests, say so; do not invent pass results.

---

## Style

- Concise, current, operational.
- Prefer tables and checklists over prose walls.
- Stable contracts, not diary entries.
- Delete stale notes; do not accumulate “formerly we…”.
- Complete sentences in commit messages and PR text; explain *why*, not only *what*.

---

## Closeout

1. Re-check changed paths against this contract and any child `AGENTS.md`.
2. Update nearest owning docs and affected parents/children.
3. Refresh Child DOX Index if structure changed.
4. Remove contradictions.
5. Run verification relevant to the change.
6. Report docs intentionally left unchanged and why (if any).

---

## Child DOX Index

Direct children (read the child before editing that subtree; children list their own descendants):

| Path | Owns |
|------|------|
| [module/AGENTS.md](module/AGENTS.md) | Kernel module `nata.ko` — datapath, ioctl, build hygiene |
| [tools/AGENTS.md](tools/AGENTS.md) | Userspace `natactl` and control-plane clients |
| [scripts/AGENTS.md](scripts/AGENTS.md) | Netns / same-ns bring-up and teardown |
| [docs/AGENTS.md](docs/AGENTS.md) | Documentation authority split; child: `docs/specs/` |
| [firmware/AGENTS.md](firmware/AGENTS.md) | FPGA gateware tree; children: `rtl/`, `sim/`, `constraints/` |
| [hardware/AGENTS.md](hardware/AGENTS.md) | Physical bridge design; children: `schematics/`, `pcb/` |

Full DOX map (every contract file):

```text
AGENTS.md                          ← you are here (repo rail)
├── module/AGENTS.md
├── tools/AGENTS.md
├── scripts/AGENTS.md
├── docs/AGENTS.md
│   └── specs/AGENTS.md
├── firmware/AGENTS.md
│   ├── rtl/AGENTS.md
│   ├── sim/AGENTS.md
│   └── constraints/AGENTS.md
└── hardware/AGENTS.md
    ├── schematics/AGENTS.md
    └── pcb/AGENTS.md
```
