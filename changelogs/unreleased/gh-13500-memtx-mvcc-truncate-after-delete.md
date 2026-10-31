## bugfix/memtx

* Fixed a crash and a wrong "Can not perform index build in a multi-statement
  transaction" error on truncating a space with secondary indexes after a
  delete from it in the same transaction with MVCC enabled (gh-13500).
