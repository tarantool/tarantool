## bugfix/replication

* The time spent fetching a snapshot no longer consumes `replication_anon_ttl`,
  so an anonymous replica is not removed right after the transfer with the WAL
  files it needs to subscribe (gh-13554).
