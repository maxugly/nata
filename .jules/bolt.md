
## 2024-05-18 - Eliminating kzalloc in Kernel Hotpaths
**Learning:** Using `kzalloc` inside a highly active network data path (`sim_tx_packet` and `sim_rx_one_packet`) causes significant overhead, introducing unnecessary memory allocation/deallocation latency and fragmentation.
**Action:** Always attempt to read/write directly to destination buffers or shared memory (e.g., using direct `memcpy` instead of intermediate allocated buffers) when in the hotpath of kernel modules, especially for high-frequency operations like packet processing.

## 2024-07-19 - NAPI SKB Allocation in Hotpaths
**Learning:** Using `dev_alloc_skb` in NAPI polling contexts misses out on per-CPU caching optimizations provided by NAPI-specific allocators.
**Action:** Use `napi_alloc_skb` instead of `dev_alloc_skb` in driver hotpaths operating under a NAPI poll loop to reduce memory allocation overhead.
