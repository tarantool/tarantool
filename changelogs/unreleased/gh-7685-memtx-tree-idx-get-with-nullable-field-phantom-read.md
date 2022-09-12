## bugfix/memtx

* Fixed possibility of phantom reads with `get` on TREE index containing
  nullable part (gh-7685).
