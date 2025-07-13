## bugfix/memtx

* Fixed a Memtx-MVCC bug that could lead to duplicates in secondary indexes
  after a rollback (gh-11660).
