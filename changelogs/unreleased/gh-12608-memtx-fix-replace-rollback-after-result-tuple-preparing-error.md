## bugfix/memtx

* Fixed a crash in memtx MVCC when `replace()` failed while preparing the old
  tuple for return after modifying indexes (gh-12608).
