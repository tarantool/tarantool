## bugfix/memtx

* Fixed a problem when MVCC tracking of pagination over a non-unique
  secondary index lost conflict (gh-13034).
