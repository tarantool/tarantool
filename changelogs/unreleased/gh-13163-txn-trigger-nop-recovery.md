## bugfix/box

* Fixed crashes in triggers during WAL recovery and replication of transactions
  containing NOP statements, transaction iterators now skip all NOPs (gh-13163).
