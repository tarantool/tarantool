## bugfix/memtx

* Fixed a bug in the memtx hash index implementation that could lead to
  uncommitted data written to a snapshot file (gh-7539).
