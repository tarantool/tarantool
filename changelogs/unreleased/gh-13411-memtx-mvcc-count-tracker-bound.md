## bugfix/memtx

* Fixed a use-after-free in a transaction that used `select` with the `offset`
  option concurrently with a `replace` that was rolled back later, with MVCC
  enabled (gh-13411).
