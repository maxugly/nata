# docs/ — Documentation Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Project documentation split by authority:

| Kind | Location | Authority |
|------|----------|-----------|
| **As-built specs** | `specs/` | Normative for current code behavior |
| **Whitepaper** | `whitepaper.md` | Rationale, electrical/protocol story — **non-normative** |

---

## Ownership

| Path | Owns |
|------|------|
| `specs/` | Contract-grade as-built documentation (see child) |
| `whitepaper.md` | Motivation, PHY/link stalemate, FPGA middleman narrative |

Root [../README.md](../README.md) is the human front door; it must not contradict `specs/` on numbers or maturity.

---

## Local Contracts

- **Specs win over whitepaper** when they disagree on implemented behavior. Fix the loser or mark whitepaper sections historical.
- Whitepaper may discuss future hardware; it must not be cited as proof that RTL is production-ready.
- New durable doc types (API reference, hardware bring-up log) get either a new subdir + `AGENTS.md` or a clear home under `specs/` with index updates.
- Do not dump generated Kbuild logs or binary blobs into `docs/`.

---

## Work Guidance

1. Behavior/ABI/layout changes → update `specs/` in the same change (parent DOX pass).
2. Pure storytelling / physics → whitepaper only; still avoid false “works today” claims.
3. README tables that restate mailbox layout or benchmarks are mirrors of specs — update both.

---

## Verification

- [ ] `specs/README.md` index lists every normative doc
- [ ] No README claim of hardware performance without `specs/08` (or successor) measurement section
- [ ] Whitepaper does not redefine packet magic/LBA map differently from specs without a “historical/proposed” label

---

## Child DOX Index

| Path | Owns |
|------|------|
| [specs/AGENTS.md](specs/AGENTS.md) | As-built specification set |
