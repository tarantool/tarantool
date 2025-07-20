## bugfix/mvcc

* Fixed an MVCC bug when a transaction performing insert-after-delete with the
  same primary key (e.g., `delete{4}` followed by `insert{4, 3}`) could create
  secondary key duplicates.
