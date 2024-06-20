## bugfix/replication

* Significantly improved replication performance by batching rows to be sent
  before dispatch. Batch size in bytes may be controlled by a new tweak
  `xrow_stream_flush_size` (default is 16 kb) (gh-10161).
