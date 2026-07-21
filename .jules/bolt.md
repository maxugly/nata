
## 2024-05-18 - Eliminating kzalloc in Kernel Hotpaths
**Learning:** Using `kzalloc` inside a highly active network data path (`sim_tx_packet` and `sim_rx_one_packet`) causes significant overhead, introducing unnecessary memory allocation/deallocation latency and fragmentation.
**Action:** Always attempt to read/write directly to destination buffers or shared memory (e.g., using direct `memcpy` instead of intermediate allocated buffers) when in the hotpath of kernel modules, especially for high-frequency operations like packet processing.
## 2024-05-18 - NAPI skb Allocator Optimization
**Learning:** The NATA network packet processing (RX path) operates in a NAPI polling context. Using generic slab allocators like `dev_alloc_skb` misses performance optimizations available in NAPI contexts.
**Action:** Use NAPI-specific allocators (`napi_alloc_skb`) in RX paths that are drained by NAPI to leverage per-CPU caches and improve memory allocation performance.
