## bugfix/replication

* Fixed replicaset bootstrap getting stuck on some nodes with `ER_READONLY` when
  there are connectivity problems (gh-7737, gh-8681).
