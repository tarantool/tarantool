## bugfix/core

* Fixed a bug when the instance hung during transition to RW
  state after promotion with a quorum greater than the
  number of registered instances in the replicaset. Now the
  transition can be continued by reducing the
  replication_synchro_quorum value (gh-11574).
