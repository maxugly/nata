# tools/ — Userspace Tools Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Userspace control utilities for NATA. Primary binary: `natactl` — ioctl client for `/dev/nata_ctl`.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `natactl.c` | CLI: `bind`, `unbind`, `status` |
| `Makefile` | Build/install/clean |

Includes ioctl layouts from `../module/nata.h` (build-time dependency on module headers).

---

## Local Contracts

- Device path is **`/dev/nata_ctl`** only.
- **Sim mode:** bind/unbind must surface `EOPNOTSUPP` clearly; do not fake success.
- **Status output** should remain human-readable telemetry; if scripts parse it, document a stable subset first.
- **CLI default LBAs** for bind (`tx=0`, `rx=100`) are **not** the kernel sim map (`128`/`0`). That mismatch is intentional for future hardware paths; keep the difference documented in [../docs/specs/05-control-plane.md](../docs/specs/05-control-plane.md).
- Do **not** commit the built `natactl` binary. Source only.

### Build

```bash
make -C tools          # -Wall -Wextra -O2
make -C tools install  # PREFIX=/usr/local by default
make -C tools clean
```

---

## Work Guidance

1. Any ioctl number or struct change: edit `module/nata.h` first, then kernel handler, then `natactl.c`, then spec 05.
2. Prefer clear errno-based messages over generic “failed”.
3. Stay a thin control plane — no packet I/O in userspace for the data path.

---

## Verification

```bash
make -C tools
# module loaded:
./tools/natactl status
./tools/natactl bind /dev/null 0 100   # expect EOPNOTSUPP in sim
```

- [ ] Builds clean with current `nata.h`
- [ ] Status works in sim
- [ ] Spec 05 updated if CLI or ABI changed

---

## Child DOX Index

None. Leaf.
