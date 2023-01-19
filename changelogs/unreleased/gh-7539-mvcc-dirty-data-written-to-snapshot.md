## bugfix/memtx

* Fixed a bug in the memtx `HASH` index implementation that could lead to
  writing uncommitted data to a snapshot file (gh-7539).
