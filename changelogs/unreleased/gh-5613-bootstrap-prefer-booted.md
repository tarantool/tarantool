## bugfix/replication

* Fixed an error when a replica, at attempt to join a cluster with exclusively
  read-only replicas available, instead of failing or retrying just decided to
  boot its own replicaset. Now it fails with an error about the other nodes
  being read-only so they can't register it (gh-5613).
