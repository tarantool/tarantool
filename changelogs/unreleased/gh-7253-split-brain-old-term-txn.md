## bugfix/raft

* Fixed a bug when a replicaset could be split into parts if an old leader
  started a new synchronous txn shortly before a new leader was going to be
  elected. This was possible if the old leader hadn't learned the new term yet
  (gh-7253).
