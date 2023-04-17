## bugfix/replication

* Fixed a bug that occurred on applier failure: a node could start an election
  without having a quorum to do this (gh-8433).
