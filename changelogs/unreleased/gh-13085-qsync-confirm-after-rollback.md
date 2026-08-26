## bugfix/raft

* Fixed a bug where an old leader could rejoin with a new one while having
  mismatched data, and this would remain undetected. This could happen when
  synchronous transactions are rolled back due to a timeout (gh-13085).
