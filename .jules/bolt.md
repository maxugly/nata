
## 2024-05-18 - Eliminating kzalloc in Kernel Hotpaths
**Learning:** Using `kzalloc` inside a highly active network data path (`sim_tx_packet` and `sim_rx_one_packet`) causes significant overhead, introducing unnecessary memory allocation/deallocation latency and fragmentation.
**Action:** Always attempt to read/write directly to destination buffers or shared memory (e.g., using direct `memcpy` instead of intermediate allocated buffers) when in the hotpath of kernel modules, especially for high-frequency operations like packet processing.

## 2026-07-23 - Leveraging napi_alloc_skb in NAPI contexts
**Learning:** In NAPI polling contexts (softirq), using `dev_alloc_skb` incurs unnecessary lock contention and overhead compared to `napi_alloc_skb`.
**Action:** Always prefer `napi_alloc_skb` over generic memory allocators when allocating socket buffers (`skb`) inside a NAPI receive hotpath to utilize per-CPU cache performance benefits.
