## bugfix/replication

* Fixed a bug when raft state wasn't sent during META_JOIN stage, which could
  lead to split-brain (gh-10089).
