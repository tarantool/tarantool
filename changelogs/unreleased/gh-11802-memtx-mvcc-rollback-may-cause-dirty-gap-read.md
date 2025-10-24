## bugfix/memtx

* Fixed a Memtx-MVCC bug that could lead to dirty gap read in secondary indexes
  after a rollback (gh-11802).
