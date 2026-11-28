## bugfix/core

* Fixed inconsistent reads of system spaces from memtx MVCC read views, which
  could cause a crash when creating a SQL constraint (gh-13838).
