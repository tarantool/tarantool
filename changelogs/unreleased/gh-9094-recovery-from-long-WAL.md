## bugfix/replication

* Now Tarantool sends heartbeats during WAL scanning when starting replication
  to a freshly subscribed replica. This prevents the relay from timing out when
  the replica needs rows from the end of a long `.xlog` file (gh-9094).
