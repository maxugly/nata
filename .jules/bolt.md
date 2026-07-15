## 2024-07-15 - Fast-path check for NAPI poll
**Learning:** `nata_poll` was unnecessarily grabbing a spinlock on an empty ring, consuming cycles during the poll loop.
**Action:** Adding a lockless peek using `check_rx_pending()` avoids the spinlock overhead entirely for the empty case.
