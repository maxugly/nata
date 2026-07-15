# scripts/ — Bring-up Scripts Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Root-operated shell helpers to load/unload the simulation module and configure interfaces. Canonical end-to-end lab path uses **network namespaces**.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata-ns-up.sh` | Load sim module, create `nata-a`/`nata-b`, move NICs, address, ping check |
| `nata-ns-down.sh` | Move NICs back, delete netns, `rmmod` |
| `nata-up.sh` | Same-namespace smoke config only (not a valid dual-host IP test) |

Shell: portable `sh`, `set -e`, require root.

---

## Local Contracts

### Defaults (overridable by env on ns scripts)

| Variable | Default |
|----------|---------|
| `NATA_NS_A` | `nata-a` |
| `NATA_NS_B` | `nata-b` |
| `NATA_ADDR_A` | `192.168.42.1/24` |
| `NATA_ADDR_B` | `192.168.42.2/24` |
| `NATA_PEER_B` | `192.168.42.2` |

### Behavioral rules

- Always load with **`target_ata_port=-1`** until hardware bind exists.
- Module path: `module/nata.ko` relative to CWD, or `./nata.ko`.
- **Up scripts are not idempotent without cleanup** — `nata-ns-up.sh` tears down prior ns/module first.
- **Down is best-effort** (`|| true` on missing ns/links) so teardown does not strand the system.
- **Same-ns script must not claim ping works** between the two IPs; comments and echoes must keep that warning.
- Prefer netns for any README/spec command that implies ARP/ICMP/TCP success.

### Operator expectations

- Need `ip` (iproute2), `ping`, root or equivalent caps.
- Moving netdevs into netns is required for same-host dual-stack tests — document why, never remove the comment without a better design.

---

## Work Guidance

1. Keep scripts boring and explicit; no hidden package installs.
2. If default addresses or ns names change, update [../docs/specs/06-simulation-and-netns.md](../docs/specs/06-simulation-and-netns.md) and root README.
3. Do not add hardware FPGA flash/program steps here until that path is real; new script + docs if so.
4. Preserve cleanup-before-up so repeated runs work.

---

## Verification

```bash
sudo ./scripts/nata-ns-up.sh    # ping -c 3 must pass
sudo ./scripts/nata-ns-down.sh
sudo ./scripts/nata-up.sh       # interfaces appear; do not require cross-ping
sudo rmmod nata 2>/dev/null || true
```

- [ ] ns-up exits 0 on clean machine with built `module/nata.ko`
- [ ] ns-down leaves no `nata-a`/`nata-b` and module unloaded when possible
- [ ] Spec 06 + README still match defaults

---

## Child DOX Index

None. Leaf.
