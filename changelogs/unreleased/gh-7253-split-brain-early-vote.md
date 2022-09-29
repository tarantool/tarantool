## bugfix/raft

* Fixed a bug when a replicaset could be split into parts if a node voted
  for another instance while having local WAL writes unfinished (gh-7253).
