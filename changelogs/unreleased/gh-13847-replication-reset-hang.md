## bugfix/replication

* Fixed a bug when removal of a replica from `replication.peers` or instance
  shutdown could hang for the duration of `replication.reconnect_timeout`
  (gh-13847).
