## bugfix/replication

* Fixed waiting for `replication_connect_timeout` during replication
  reconfiguration when every connection attempt had already completed and
  at least one applier had stopped after a permanent error (gh-13656).
