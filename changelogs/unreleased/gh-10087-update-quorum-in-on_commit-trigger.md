## feature/replication

* Now the synchronous replication quorum is updated after the cluster
  reconfiguration change is confirmed by a quorum rather than immediately after
  persisting the configuration change in the WAL (gh-10087).
