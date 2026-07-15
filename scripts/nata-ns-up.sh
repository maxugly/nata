#!/bin/sh
# Bring up NATA simulation with nata0 and nata1 in separate network namespaces.
#
# Same-host loopback without netns fails ARP and IP: the kernel treats both
# addresses as local and will not complete neighbor discovery between them.
# Isolating each NIC in its own netns makes the two sides look like hosts.

set -e

NS_A="${NATA_NS_A:-nata-a}"
NS_B="${NATA_NS_B:-nata-b}"
ADDR_A="${NATA_ADDR_A:-192.168.42.1/24}"
ADDR_B="${NATA_ADDR_B:-192.168.42.2/24}"
PEER_B="${NATA_PEER_B:-192.168.42.2}"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root." >&2
    exit 1
fi

# Resolve module path relative to repo root or CWD
MODULE=""
if [ -f "module/nata.ko" ]; then
    MODULE="module/nata.ko"
elif [ -f "nata.ko" ]; then
    MODULE="nata.ko"
else
    echo "Error: nata.ko not found. Build with: make -C module" >&2
    exit 1
fi

cleanup_ns() {
    # Move interfaces back if present so rmmod can succeed later
    ip netns exec "$NS_A" ip link set nata0 netns 1 2>/dev/null || true
    ip netns exec "$NS_B" ip link set nata1 netns 1 2>/dev/null || true
    ip netns del "$NS_A" 2>/dev/null || true
    ip netns del "$NS_B" 2>/dev/null || true
}

echo "Cleaning previous NATA netns / module state..."
cleanup_ns
rmmod nata 2>/dev/null || true

echo "Loading NATA simulation module ($MODULE)..."
insmod "$MODULE" target_ata_port=-1

echo "Waiting for nata0 / nata1 to register..."
i=0
while [ "$i" -lt 20 ]; do
    if [ -d /sys/class/net/nata0 ] && [ -d /sys/class/net/nata1 ]; then
        break
    fi
    i=$((i + 1))
    sleep 0.1
done

if [ ! -d /sys/class/net/nata0 ] || [ ! -d /sys/class/net/nata1 ]; then
    echo "Error: nata0 or nata1 failed to appear after module load." >&2
    exit 1
fi

echo "Creating network namespaces $NS_A and $NS_B..."
ip netns add "$NS_A"
ip netns add "$NS_B"

echo "Moving interfaces into namespaces..."
ip link set nata0 netns "$NS_A"
ip link set nata1 netns "$NS_B"

echo "Configuring $NS_A: nata0 $ADDR_A"
ip netns exec "$NS_A" ip link set lo up
ip netns exec "$NS_A" ip addr flush dev nata0 2>/dev/null || true
ip netns exec "$NS_A" ip addr add "$ADDR_A" dev nata0
ip netns exec "$NS_A" ip link set nata0 up

echo "Configuring $NS_B: nata1 $ADDR_B"
ip netns exec "$NS_B" ip link set lo up
ip netns exec "$NS_B" ip addr flush dev nata1 2>/dev/null || true
ip netns exec "$NS_B" ip addr add "$ADDR_B" dev nata1
ip netns exec "$NS_B" ip link set nata1 up

echo "Neighbor tables before ping:"
ip netns exec "$NS_A" ip neigh show dev nata0 || true
ip netns exec "$NS_B" ip neigh show dev nata1 || true

echo "Pinging $PEER_B from $NS_A (nata0)..."
ip netns exec "$NS_A" ping -c 3 "$PEER_B"

echo
echo "Neighbor tables after ping:"
ip netns exec "$NS_A" ip neigh show dev nata0 || true
ip netns exec "$NS_B" ip neigh show dev nata1 || true

echo
echo "Interface stats:"
ip netns exec "$NS_A" ip -s link show nata0
ip netns exec "$NS_B" ip -s link show nata1

echo
echo "NATA netns simulation is up."
echo "  Shell in A:  ip netns exec $NS_A bash"
echo "  Shell in B:  ip netns exec $NS_B bash"
echo "  Tear down:   sudo ./scripts/nata-ns-down.sh"
