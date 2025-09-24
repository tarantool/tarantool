## bugfix/raft

* Fix a bug that a snapshot file could contain an outdated synchronous
  replication's confirmation LSN or a term. That was only possible when the
  synchronous replication is being used (gh-11754).
