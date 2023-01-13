## feature/core

* Now if a join fails with a non-critical error, such as `ER_READONLY`,
  `ER_ACCESS_DENIED`, or a network-related error, the instance tries
  to find a new master to join to and tries again (gh-6126).

* Renamed replication states when a replica is joining. Now when querying
  `box.info.replication[id].upstream.status` during join, you will
  see either `wait_snapshot` or `fetch_snapshot` instead of
  `initial_join` (gh-6126).
