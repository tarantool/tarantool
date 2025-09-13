## bugfix/mvcc

* Fixed an MVCC bug that could lead to dirty gap read in secondary indexes
after a rollback.
