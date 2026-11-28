## feature/core

- Add experimental memtx VECTOR indexes on fixed-size embedding fields.
  Local and IProto SELECT support bounded NEIGHBOR, and local search accepts
  filters and can return distances. VECTOR read views pin immutable graph
  snapshots and the frozen primary index.
- Charge graph storage and cooperative rebuilds to memtx quota. Reuse
  slots once tuple versions are safe to collect. Expose memory, slot,
  search, and rebuild counters through `index:stat()`.
