## bugfix/memtx

* Fixed a crash when transaction concurrent with background index build
  was rolled back due to WAL failure (gh-10620).
