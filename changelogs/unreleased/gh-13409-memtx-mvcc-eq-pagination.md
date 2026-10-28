## bugfix/memtx

* Fixed a crash and a missed conflict when an `EQ` or `REQ` select
  resumed from an `after` position under MVCC (gh-13409).
