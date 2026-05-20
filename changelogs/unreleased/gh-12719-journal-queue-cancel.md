## bugfix/replication

* Fixed a crash that could occur when a replication source was removed from
  `box.cfg.replication` at the same time as the node reached its maximum WAL
  queue size (configured by `box.cfg.wal_queue_max_size`) (gh-12719).
