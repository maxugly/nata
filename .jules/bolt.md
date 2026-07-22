
## 2024-05-18 - Eliminating kzalloc in Kernel Hotpaths
**Learning:** Using `kzalloc` inside a highly active network data path (`sim_tx_packet` and `sim_rx_one_packet`) causes significant overhead, introducing unnecessary memory allocation/deallocation latency and fragmentation.
**Action:** Always attempt to read/write directly to destination buffers or shared memory (e.g., using direct `memcpy` instead of intermediate allocated buffers) when in the hotpath of kernel modules, especially for high-frequency operations like packet processing.
## 2026-07-22 - Leveraging NAPI Allocators for RX Path
**Learning:** In NAPI polling contexts, using generic slab allocators like `dev_alloc_skb` incurs unnecessary locking overhead and cache misses compared to NAPI-specific allocators.
**Action:** Use `napi_alloc_skb` for network RX packet allocation inside NAPI poll routines to leverage per-CPU caches and improve throughput.
