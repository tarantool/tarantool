## bugfix/replication

* Fixed the inability to add a new replica to the replicaset if the user has
  manually made space `_cluster` synchronous. Now the synchronous replication
  quorum is updated after the `_cluster` change is confirmed by a quorum rather
  than immediately after persisting the configuration change in the WAL
  (gh-10087).
