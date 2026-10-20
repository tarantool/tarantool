## bugfix/memtx

* Fixed a crash on a write to a space with a functional index whose row was
  deleted from `_func_index`, with MVCC enabled (gh-13477).
