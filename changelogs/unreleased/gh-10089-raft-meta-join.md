## bugfix/replication

* Fixed a bug when the Raft state wasn't sent during the `META_JOIN` stage,
  which could lead to split-brain (gh-10089).
