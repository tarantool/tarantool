## feature/replication

* Now all replicas have WAL GC consumers persisted in the `_gc_consumers`
  space (gh-10154). The `wal_cleanup_delay` option is no longer needed,
  so it is deprecated.
