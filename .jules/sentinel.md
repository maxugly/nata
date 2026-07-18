## 2024-07-14 - [Kernel Panic via Malformed NATA Packets]
**Vulnerability:** The NATA virtual network driver's RX handler (`sim_rx_one_packet`) only checked for empty packets (`hdr.len == 0`), allowing packets smaller than an Ethernet header (1-13 bytes) to be passed to `eth_type_trans()`.
**Learning:** `eth_type_trans()` unconditionally pulls `ETH_HLEN` (14 bytes) from the skb. Passing a smaller packet causes a negative length wrap-around and an out-of-bounds memory read, leading to a direct kernel panic. Additionally, `nata_ioctl` was using `spin_lock` instead of `spin_lock_bh` in process context, introducing a local deadlock risk.
**Prevention:** Always validate that incoming raw network frames from untrusted sources are at least `ETH_HLEN` bytes before injecting them into the kernel network stack. In kernel drivers, always use `spin_lock_bh` when locking data shared with softirq contexts (like network transmission).

## 2026-07-18 - [Authorization Bypass on Admin ioctls]
**Vulnerability:** The NATA control device `/dev/nata_ctl` allowed any user with write access to the device node to execute administrative `NATA_IOC_BIND` and `NATA_IOC_UNBIND` commands, missing an explicit capability check. While a udev rule restricts device access to the `netdev` group, defense-in-depth requires kernel-level enforcement.
**Learning:** Depending solely on userspace udev rules (`MODE="0660", GROUP="netdev"`) for securing sensitive operations is insufficient. If permissions are misconfigured or bypassed, unprivileged users could rebind the virtual NIC to arbitrary storage devices.
**Prevention:** Always use `capable(CAP_NET_ADMIN)` or similar capability checks inside the kernel ioctl handler for operations that alter system-wide networking or storage state.
