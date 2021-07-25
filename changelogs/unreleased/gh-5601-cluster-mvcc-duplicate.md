## bugfix/replication

* Fixed a rare error appearing when MVCC (`box.cfg.memtx_use_mvcc_engine`) was
  enabled and more than one replica was joined to a cluster. The join could fail
  with the error `"ER_TUPLE_FOUND: Duplicate key exists in unique index
  'primary' in space '_cluster'"`. The same could happen at bootstrap of a
  cluster having >= 3 nodes (gh-5601).
