## bugfix/memtx

* Fixed a bug when a transaction using MVCC missed a conflict after
  reading a paginated non-unique secondary index (gh-13034).
