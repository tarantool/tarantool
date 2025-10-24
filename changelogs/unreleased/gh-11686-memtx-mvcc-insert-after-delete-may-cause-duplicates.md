## bugfix/memtx

* Fixed a Memtx-MVCC bug when a transaction performing insert-after-delete
  with the same primary key (for example, `delete{4}` followed by
  `insert{4, 3}`) could create secondary key duplicates (gh-11686).
