## feature/raft

* Introduce strict fencing, which tries its best to allow at most one
  leader in cluster in any moment in time. This is achived by setting
  connection death timeout on current leader to half the time compared to
  followers (assuming replication_timeout is the same on every replica)
  (gh-7110).
