#!/bin/sh
# Bring up NATA simulated interfaces in the *same* network namespace.
#
# Prefer scripts/nata-ns-up.sh for a real ping/ARP test on one machine:
# both addresses are local to the host stack here, so neighbor discovery
# between nata0 and nata1 will not complete without separate netns.

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root." >&2
    exit 1
fi

rmmod nata 2>/dev/null || true

echo "Loading NATA simulation module..."
if [ -f "module/nata.ko" ]; then
    insmod "module/nata.ko" target_ata_port=-1
elif [ -f "nata.ko" ]; then
    insmod "nata.ko" target_ata_port=-1
else
    echo "Error: nata.ko not found. Please build the module first." >&2
    exit 1
fi

echo "Waiting 1 second for virtual devices to register..."
sleep 1

if [ ! -d "/sys/class/net/nata0" ] || [ ! -d "/sys/class/net/nata1" ]; then
    echo "Error: nata0 or nata1 interfaces failed to appear." >&2
    exit 1
fi

echo "Configuring nata0 (192.168.42.1/24)..."
ip addr flush dev nata0 2>/dev/null || true
ip addr add 192.168.42.1/24 dev nata0

echo "Configuring nata1 (192.168.42.2/24)..."
ip addr flush dev nata1 2>/dev/null || true
ip addr add 192.168.42.2/24 dev nata1

echo "Bringing up virtual interfaces..."
ip link set nata0 up
ip link set nata1 up

echo
echo "NATA same-namespace simulation is up (nata0 / nata1)."
echo "Note: ping between these two IPs on one host will fail ARP/IP"
echo "local-delivery checks. For a full loopback test use:"
echo "  sudo ./scripts/nata-ns-up.sh"
