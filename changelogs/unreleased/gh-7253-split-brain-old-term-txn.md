## bugfix/raft

* Fixed a bug when a replicaset could be split in parts if an old leader started
  a new synchronous txn shortly before a new leader was going to be elected.
  That was possible if the old leader didn't learn the new term yet (gh-7253).
