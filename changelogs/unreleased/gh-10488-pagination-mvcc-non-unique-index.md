## bugfix/memtx

* Fixed a crash when using pagination over a non-unique index with range
  requests and MVCC enabled (gh-10448).
