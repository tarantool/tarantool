## feature/raft

* Introduced strict fencing, which tries its best to allow at most one
  leader in cluster in any moment in time. This is achieved by setting
  connection death timeout on the current leader to half the time compared to
  followers (assuming the `replication_timeout` is the same on every replica)
  (gh-7110).
