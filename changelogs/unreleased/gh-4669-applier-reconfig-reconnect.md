## bugfix/replication

* Fixed replica reconnecting to a living master on any `box.cfg{replication=...}`
  change. Such reconnects could lead to replica failing to restore connection
  for `replication_timeout` seconds (gh-4669).
