#!/bin/sh
# One-shot sim bench for 32-slot + NAPI + TX backpressure.
# Run from repo root with: sudo ./scripts/nata-bench-once.sh
# Writes results to /tmp/nata-bench-results.txt (and stdout).
set -e

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUT="${NATA_BENCH_OUT:-/tmp/nata-bench-results.txt}"
{
	echo "=== NATA bench $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
	echo "host=$(hostname) kernel=$(uname -r)"
	echo "cwd=$ROOT"
	echo

	if [ "$(id -u)" -ne 0 ]; then
		echo "ERROR: run as root (sudo $0)" >&2
		exit 1
	fi

	# Clean slate
	./scripts/nata-ns-down.sh 2>/dev/null || true
	./scripts/nata-ns-up.sh

	echo "=== ping (50) ==="
	ip netns exec nata-a ping -c 50 -i 0.05 192.168.42.2 || true
	echo

	echo "=== iperf3 TCP A→B (10s) ==="
	ip netns exec nata-b iperf3 -s -1 -D
	sleep 0.5
	ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 || true
	echo

	echo "=== iperf3 TCP B→A reverse (10s) ==="
	ip netns exec nata-b iperf3 -s -1 -D
	sleep 0.5
	ip netns exec nata-a iperf3 -c 192.168.42.2 -t 10 -R || true
	echo

	echo "=== iperf3 UDP unlimited (10s) ==="
	ip netns exec nata-b iperf3 -s -1 -D
	sleep 0.5
	ip netns exec nata-a iperf3 -c 192.168.42.2 -u -b 0 -t 10 || true
	echo

	echo "=== natactl status ==="
	if [ -x ./tools/natactl ]; then
		./tools/natactl status || true
	else
		echo "(tools/natactl missing — make -C tools)"
	fi
	echo

	./scripts/nata-ns-down.sh
	echo "=== done ==="
} 2>&1 | tee "$OUT"

echo "Results written to $OUT" >&2
