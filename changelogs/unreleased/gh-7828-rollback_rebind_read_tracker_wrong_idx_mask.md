## bugfix/memtx

* Fixed phantom read possible after reads from different indexes followed by a
  rollback (gh-7828).
