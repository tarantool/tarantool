## bugfix/raft

* Fixed a bug when a replicaset could be split in parts if a node during
  elections voted for another instance while having some local WAL writes not
  finished (gh-7253).
