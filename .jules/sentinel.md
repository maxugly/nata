## 2024-07-14 - [Kernel Panic via Malformed NATA Packets]
**Vulnerability:** The NATA virtual network driver's RX handler (`sim_rx_one_packet`) only checked for empty packets (`hdr.len == 0`), allowing packets smaller than an Ethernet header (1-13 bytes) to be passed to `eth_type_trans()`.
**Learning:** `eth_type_trans()` unconditionally pulls `ETH_HLEN` (14 bytes) from the skb. Passing a smaller packet causes a negative length wrap-around and an out-of-bounds memory read, leading to a direct kernel panic. Additionally, `nata_ioctl` was using `spin_lock` instead of `spin_lock_bh` in process context, introducing a local deadlock risk.
**Prevention:** Always validate that incoming raw network frames from untrusted sources are at least `ETH_HLEN` bytes before injecting them into the kernel network stack. In kernel drivers, always use `spin_lock_bh` when locking data shared with softirq contexts (like network transmission).

## 2026-07-15 - [Privilege Escalation via Missing Capability Check in NATA IOCTL]
**Vulnerability:** The NATA control device `/dev/nata_ctl` did not enforce proper authorization checks (like `capable(CAP_NET_ADMIN)`) for `NATA_IOC_BIND` and `NATA_IOC_UNBIND` ioctl commands.
**Learning:** Because udev rules (`99-nata.rules`) mapped the character device to `0660` with group `netdev`, any user in the `netdev` group could access the ioctl commands. Without explicit `CAP_NET_ADMIN` checks in the kernel module code, this allowed unprivileged users to execute destructive bind/unbind operations.
**Prevention:** Always verify caller privileges using `capable()` or `ns_capable()` in kernel drivers for any ioctl commands that perform administrative or state-altering actions, especially when the device node is accessible to non-root users via udev rules.
