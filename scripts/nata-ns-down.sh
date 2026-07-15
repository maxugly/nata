#!/bin/sh
# Tear down NATA simulation network namespaces and unload the module.

set -e

NS_A="${NATA_NS_A:-nata-a}"
NS_B="${NATA_NS_B:-nata-b}"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root." >&2
    exit 1
fi

echo "Moving nata interfaces back to the root namespace (if present)..."
ip netns exec "$NS_A" ip link set nata0 netns 1 2>/dev/null || true
ip netns exec "$NS_B" ip link set nata1 netns 1 2>/dev/null || true

echo "Deleting namespaces..."
ip netns del "$NS_A" 2>/dev/null || true
ip netns del "$NS_B" 2>/dev/null || true

echo "Unloading nata module..."
rmmod nata 2>/dev/null || true

echo "NATA netns simulation torn down."
