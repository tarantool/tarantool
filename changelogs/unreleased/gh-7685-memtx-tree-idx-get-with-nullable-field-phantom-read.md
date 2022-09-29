## bugfix/memtx

* Fixed possible phantom reads with `get` on TREE indexes containing
  nullable parts (gh-7685).
