# docs/specs — As-Built Spec Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md) · Root: [../../AGENTS.md](../../AGENTS.md)

This directory is the **normative contract** for current NATA behavior. It is not a design pitch. It is not a roadmap dressed as truth.

---

## Purpose

- Document what the repository **implements today**
- Name what is incomplete without softening language
- Keep protocol, mailbox, kernel, control plane, sim, RTL, and performance descriptions **identical** where they share facts
- Give implementers a checklist of remaining work instead of hope

Non-normative narrative lives in [../whitepaper.md](../whitepaper.md). Do not copy whitepaper claims into these files unless the code does that thing.

---

## Ownership

| Doc | Sole focus |
|-----|------------|
| [README.md](README.md) | Index, status tags legend, source-of-truth code map |
| [01-system-overview.md](01-system-overview.md) | Goals, modes, maturity matrix, non-goals |
| [02-packet-format.md](02-packet-format.md) | On-mailbox header and frame rules |
| [03-mailbox-memory-map.md](03-mailbox-memory-map.md) | 128 KiB layout, LBA ownership, dual-port model |
| [04-kernel-module.md](04-kernel-module.md) | Module lifecycle, netdevs, threads, locking, footguns |
| [05-control-plane.md](05-control-plane.md) | `/dev/nata_ctl`, ioctls, `natactl` |
| [06-simulation-and-netns.md](06-simulation-and-netns.md) | Netns topology, scripts, lab commands |
| [07-fpga-rtl.md](07-fpga-rtl.md) | As-built RTL stubs + gaps checklist |
| [08-performance.md](08-performance.md) | Measured numbers, methodology, design limits |

---

## Local Contracts

### File header (required on normative docs)

Every numbered spec (`01`–`08`) must open with roughly:

- **Spec version** (e.g. `0.1 (as-built)`)
- **Status** (`IMPLEMENTED` / `PARTIAL` / mix stated clearly)
- **Normative code** paths (files that define the behavior)

The word **as-built** means: derived from the tree at the baseline date, not from intent alone.

### Status vocabulary (use these words)

| Tag | Meaning |
|-----|---------|
| **IMPLEMENTED** | In tree and exercised on the sim path (or otherwise proven) |
| **PARTIAL** | Structure or API exists; behavior incomplete or stubbed |
| **PLANNED** | Intent only; no working path |
| **TBD** | Unknown / not yet specified |
| **stub** | Placeholder logic that must not be mistaken for production |
| **N/A** | Not applicable in the mode under discussion |

### Shared facts (do not diverge)

These values must match across docs and code. Change them only with a coordinated code + multi-doc update:

| Fact | Canonical value |
|------|-----------------|
| Mailbox bytes | 131072 (128 KiB) |
| Sector size | 512 |
| Sector count | 256 (LBA 0–255) |
| Lower half | LBA 0–127 — nata1 TX / nata0 RX |
| Upper half | LBA 128–255 — nata0 TX / nata1 RX |
| Header size | 16 bytes |
| Magic | `0x4E415441` (`NATA`) |
| RX len window | `ETH_HLEN` … `ETH_FRAME_LEN` |
| Queue depth | **8 slots per direction** (drop when full; no overwrite) |
| Slot size | 16 sectors / 8192 bytes (`valid` + header + payload) |

If you edit one of these in code, update **02, 03, 04, 07** (as applicable), **README.md** (repo root), and **01** maturity notes in the same task.

### What “real specs” require here

This set was praised for four habits. Keep them:

1. **Ugly numbers in tables** — retransmits, loss %, overwrite semantics, with a one-line cause (see `08`).
2. **Confusing parts explained** — e.g. `is_dev0` historical naming in `04`; CLI bind LBA defaults vs kernel sim map in `05`.
3. **RTL gaps as a checklist** — `07` ends with concrete missing production items; never imply GTP/FIS path is done.
4. **Cross-document consistency** — four docs, one mailbox; one header; one LBA story.

### Performance doc rules

- Record environment: kernel, CPU, date, topology.
- Prefer full iperf3/ping summaries; promote stable aggregates to the summary table.
- Re-measure and update when design changes or results move **>15%**.
- Never delete bad numbers after a “fix”; explain them.

### What not to put here

- Marketing comparisons that the project has not measured
- Hardware throughput claims without dual-host data
- Silent removal of gap checklists
- Duplicate essays of the whitepaper

---

## Work Guidance

When adding a feature:

1. Implement and verify in sim where possible.
2. Update the owning numbered doc(s) in this directory.
3. Adjust status tags (`PARTIAL` → `IMPLEMENTED`, etc.) honestly.
4. If the feature is still incomplete, extend the **Gaps** checklist instead of upgrading the status.
5. If a new durable concern does not fit 01–08, add a numbered doc and register it in [README.md](README.md) and the parent Child DOX Index.

When only documenting:

- Read the code listed in README source-of-truth table first.
- Cite constants and function names that exist.
- If you cannot find an implementation, mark **PLANNED** or **TBD** — do not invent.

---

## Verification

- [ ] Status tags match reality for every capability touched
- [ ] Shared facts table still matches `module/nata.h`, `nata_blk.c`, `nata_main.c`, `dual_port_ram.v`
- [ ] No doc claims hardware bind or full SATA device IP unless code path exists
- [ ] Performance section still states **simulation only** unless hardware numbers were added under a new measured subsection
- [ ] Root README benchmark / FAQ snippets still agree with `08` if those were updated

---

## Child DOX Index

None. Leaf contract.
