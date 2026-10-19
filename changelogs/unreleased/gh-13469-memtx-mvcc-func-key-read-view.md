## bugfix/memtx

* Fixed a crash when using `count` on a functional index in a read view with
  MVCC enabled (gh-13469).
