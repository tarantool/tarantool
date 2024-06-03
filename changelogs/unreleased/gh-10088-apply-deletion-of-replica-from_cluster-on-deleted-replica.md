## feature/replication

* A replica deleted from the `_cluster` space now applies its own deletion and
  does not try to rejoin (gh-10088).
