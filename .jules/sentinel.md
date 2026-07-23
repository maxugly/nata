## 2024-07-23 - Missing Authorization on Privileged IOCTLs
**Vulnerability:** The NATA control device `/dev/nata_ctl` allowed any user to execute privileged `NATA_IOC_BIND` and `NATA_IOC_UNBIND` ioctl commands, potentially leading to unauthorized manipulation of network interface backends.
**Learning:** Kernel modules must explicitly check user capabilities (e.g., `capable(CAP_NET_ADMIN)`) before executing sensitive configuration commands, even if the device file has restrictive permissions.
**Prevention:** Always implement capability checks for operations that affect system or network configuration, adhering to the principle of least privilege.
