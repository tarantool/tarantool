## bugfix/memtx

* Fixed a crash on continuing a reverse iterator with MVCC enabled after the
  tuple it pointed to was removed from the index, e.g. because its transaction
  was aborted (gh-13403).
