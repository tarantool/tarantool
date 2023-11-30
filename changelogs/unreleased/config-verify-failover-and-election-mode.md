## bugfix/config

* Now `replication.election_mode` values other than `off` are possible only if
  `replication.failover` value is `election` (gh-9431).
