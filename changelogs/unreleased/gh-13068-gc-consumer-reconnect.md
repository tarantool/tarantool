## bugfix/replication

* Fixed a bug where a replica's garbage collection consumer retained outdated
  WAL files after reconnecting to an upstream with a newer vclock (gh-13068).
