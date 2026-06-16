## bugfix/replication

* Fixed an ordering bug in WAL batching that could make a joining replica miss
  rows written by the master (gh-11028).
