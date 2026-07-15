# module/ — Kernel Module Contract (DOX child)

Parent: [../AGENTS.md](../AGENTS.md)

---

## Purpose

Out-of-tree Linux kernel module `nata.ko`: dual virtual NICs, simulated mailbox datapath, control misc device. This is the only fully exercised packet path today.

---

## Ownership

| File | Responsibility |
|------|----------------|
| `nata.h` | Shared types, packet header, ioctl ABI, priv layout, function decls |
| `nata_main.c` | Module init/exit, kthreads, waitqueues, miscdev, ioctl, LBA bases, mailbox alloc |
| `nata_net.c` | `nata0`/`nata1` registration, `ndo_*`, xmit → sim TX + peer wake |
| `nata_blk.c` | Mailbox bounds, `sim_tx_packet` / `sim_rx_one_packet`, barriers, seq consume-on-drop |
| `Makefile` | Kbuild glue (`nata-objs`) |
| `99-nata.rules` | udev for `/dev/nata_ctl` (`0660`, group `netdev`) |

---

## Local Contracts

- **Default mode is simulation.** `target_ata_port=-1`. Do not claim hardware bind works until a real path exists.
- **One global `nata_priv`.** No multi-instance bridges.
- **Mailbox:** 131072 bytes; TX0/RX1 @ LBA 128; TX1/RX0 @ LBA 0. Matches `docs/specs/03`.
- **Publication order:** payload, then `smp_wmb()`, then header. RX: validate, `smp_rmb()`, copy, inject.
- **Bad frames must not busy-loop.** Invalid len / overflow / alloc fail / `NET_RX_DROP` → advance `last_rx_seq`, bump `dropped_blocks`.
- **`is_dev0`:** nonzero = nata0 side. Historical name; document, do not “cleverly” invert without updating all call sites + specs.
- **Ioctl ABI** in `nata.h` is shared with `tools/`. Changing structs is a coordinated break.
- **Locking:** `spin_lock_bh(&priv->lock)` around full TX publish and RX consume. `check_rx_pending` may peek unlocked.
- **Always `NETDEV_TX_OK` today** and consumes skb; no mailbox backpressure. If you add backpressure, update specs 04 and 08.

### Build products (never commit)

`*.o`, `*.ko`, `*.mod`, `*.mod.c`, `Module.symvers`, `modules.order`, `.*.cmd` — gitignored.

---

## Work Guidance

1. Read [../docs/specs/04-kernel-module.md](../docs/specs/04-kernel-module.md) and [02](../docs/specs/02-packet-format.md)/[03](../docs/specs/03-mailbox-memory-map.md) before datapath edits.
2. Prefer netns tests after functional changes (`../scripts/nata-ns-up.sh`).
3. Keep hot path small; no unbounded loops on mailbox contents.
4. Footguns discovered in code → short section in as-built specs, not only commit message.
5. Hardware path scaffolding: keep returning explicit errors (`-EOPNOTSUPP` / `-EINVAL`); do not silent-no-op.

---

## Verification

```bash
make -C module
# with root:
sudo ./scripts/nata-ns-up.sh
sudo ip netns exec nata-a ping -c 3 192.168.42.2
# optional:
make -C tools && ./tools/natactl status
```

- [ ] Module loads without oops
- [ ] Both netdevs register
- [ ] Ping across netns works for encapsulation/seq changes
- [ ] Specs 02–05 updated if header, mailbox, threads, or ioctl changed

---

## Child DOX Index

None. Leaf.
