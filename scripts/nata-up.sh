#!/bin/sh
# Portable POSIX-compliant script to bring up NATA simulated interfaces.

set -e

# Verify root privileges
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root." >&2
    exit 1
fi

# Quietly unload the module if it's already loaded to allow repeated runs
rmmod nata 2>/dev/null || true

# Load NATA simulation module with target_ata_port=-1 to force simulation mode
echo "Loading NATA simulation module..."
if [ -f "module/nata.ko" ]; then
    insmod "module/nata.ko" target_ata_port=-1
elif [ -f "nata.ko" ]; then
    insmod "nata.ko" target_ata_port=-1
else
    echo "Error: nata.ko not found. Please build the module first." >&2
    exit 1
fi

# Wait 1 second to allow the virtual devices to register
echo "Waiting 1 second for virtual devices to register..."
sleep 1

# Check for existence of nada0 and nada1 interfaces
if [ ! -d "/sys/class/net/nada0" ] || [ ! -d "/sys/class/net/nada1" ]; then
    echo "Error: nada0 or nada1 interfaces failed to appear." >&2
    exit 1
fi

# Assign IP addresses
echo "Configuring nada0 (10.0.0.1/24)..."
ip addr flush dev nada0 2>/dev/null || true
ip addr add 10.0.0.1/24 dev nada0

echo "Configuring nada1 (10.0.0.2/24)..."
ip addr flush dev nada1 2>/dev/null || true
ip addr add 10.0.0.2/24 dev nada1

# Bring interfaces up
echo "Bringing up virtual interfaces..."
ip link set nada0 up
ip link set nada1 up

# Verify traffic flow by executing a 3-count ping from nada0 to nada1
echo "Verifying traffic flow (pinging nada1 from nada0)..."
ping -c 3 -I nada0 10.0.0.2

echo "NATA simulation setup complete."
