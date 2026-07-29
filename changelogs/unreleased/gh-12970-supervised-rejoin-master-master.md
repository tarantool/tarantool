## bugfix/config

* Fixed a bug where an instance rebootstrapped with an empty data directory
  could start in the RW mode alongside an already appointed leader with
  `replication.failover = supervised` and `replication.bootstrap_strategy =
  auto`, if the instance had the lexicographically minimal name in the
  replicaset. The bootstrap leader candidate now starts with
  `box.cfg.read_only = 'unless_bootstrap'`, so a rejoined instance never
  goes through a writable state at all (gh-12970).
