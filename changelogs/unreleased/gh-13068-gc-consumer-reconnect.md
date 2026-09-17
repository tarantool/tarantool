## bugfix/replication

* Fixed a replica's garbage collection consumer retaining outdated WAL files
  after reconnecting to an upstream with a newer vclock (gh-13068).
