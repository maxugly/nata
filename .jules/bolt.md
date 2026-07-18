
## 2024-05-18 - Eliminating kzalloc in Kernel Hotpaths
**Learning:** Using `kzalloc` inside a highly active network data path (`sim_tx_packet` and `sim_rx_one_packet`) causes significant overhead, introducing unnecessary memory allocation/deallocation latency and fragmentation.
**Action:** Always attempt to read/write directly to destination buffers or shared memory (e.g., using direct `memcpy` instead of intermediate allocated buffers) when in the hotpath of kernel modules, especially for high-frequency operations like packet processing.

## 2024-05-18 - Replacing dev_alloc_skb with napi_alloc_skb in NAPI Polling Context
**Learning:** Using `dev_alloc_skb` in the NAPI poll loop (`sim_rx_dequeue` / `nata_poll`) relies on the generic kernel slab allocator. This can introduce unnecessary lock contention and latency compared to NAPI's specialized, per-CPU cached allocations.
**Action:** When allocating `sk_buff` structures within a NAPI polling context, always utilize `napi_alloc_skb` (and pass the `struct napi_struct` down into worker functions as needed) to leverage these per-CPU caches for improved throughput and lower latency.
