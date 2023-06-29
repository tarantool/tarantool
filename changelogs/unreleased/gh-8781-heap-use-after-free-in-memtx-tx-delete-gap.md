## bugfix/memtx

* Fixed a heap-use-after-free bug in the transaction manager, which could occur
  when performing a DDL operation concurrently with a transaction on the same
  space (gh-8781).
