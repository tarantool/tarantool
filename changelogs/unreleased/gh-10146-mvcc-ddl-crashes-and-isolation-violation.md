## bugfix/memtx

* Fixed several bugs when DDL with MVCC enabled could lead to a crash
  or violate isolation of other transactions (gh-10146).
