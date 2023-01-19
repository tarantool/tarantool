## feature/core

* Introduced pagination support for memtx and vinyl tree indexes. It is now
  possible to resume `pairs` and `select` from the position where the last
  call stopped (gh-7639).
