## bugfix/config

* Fixed an issue when a leader fails to start with the `attempt to index a nil
  value` error if a config with all UUIDs set is used during a cluster's
  bootstrap (gh-9572).
