## bugfix/raft

* Fixed a bug when an old leader could rejoin with a new one while having
  mismatching data, and this would remain undetected. Could happen when
  synchronous transactions are getting rolled back by timeout (gh-13085).
