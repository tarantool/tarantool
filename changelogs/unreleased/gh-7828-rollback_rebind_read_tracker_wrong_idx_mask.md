## bugfix/memtx

* Fixed a phantom read that could happen after reads from different indexes
  followed by a rollback (gh-7828).
