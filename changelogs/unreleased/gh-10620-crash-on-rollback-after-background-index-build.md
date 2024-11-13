## bugfix/memtx

* Fixed a crash when a transaction that was processed concurrently with
  background index build was rolled back due to WAL failure (gh-10620).
