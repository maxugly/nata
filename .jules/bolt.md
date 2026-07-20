
## 2024-05-18 - Eliminating kzalloc in Kernel Hotpaths
**Learning:** Using `kzalloc` inside a highly active network data path (`sim_tx_packet` and `sim_rx_one_packet`) causes significant overhead, introducing unnecessary memory allocation/deallocation latency and fragmentation.
**Action:** Always attempt to read/write directly to destination buffers or shared memory (e.g., using direct `memcpy` instead of intermediate allocated buffers) when in the hotpath of kernel modules, especially for high-frequency operations like packet processing.

## 2024-05-19 - NAPI Allocator for Cache Locality
**Learning:** Using `dev_alloc_skb` or generic slab allocators in a NAPI polling context (like the network RX hotpath) causes cache thrashing and lowers performance compared to NAPI-specific allocators.
**Action:** Always use `napi_alloc_skb` over `dev_alloc_skb` inside a NAPI context to leverage per-CPU caches and improve packet processing performance.
