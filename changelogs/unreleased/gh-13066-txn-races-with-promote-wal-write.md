## bugfix/replication

* Fixed a debug-build crash which could happen when a quorum of acks got
  collected for a synchronous transaction while a `PROMOTE` or `DEMOTE` was
  being written to the journal (gh-13066).
