## bugfix/memtx

* Fixed `select` with the `LE` iterator in the memtx ``TREE`` index returning
  deleted tuples (gh-7432).
