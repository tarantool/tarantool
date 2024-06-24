## bugfix/core

* Fixed crashes when creating, altering or dropping a space or its index
  concurrently with DML requests using memtx MVCC (gh-10146).
