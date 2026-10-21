## bugfix/memtx

* Fixed a use-after-free in a transaction that used `select` with the `offset`
  option on a functional index concurrently with an update of the indexed
  tuple, with MVCC enabled (gh-13497).
