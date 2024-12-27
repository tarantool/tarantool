## bugfix/memtx

* Fixed a crash when memtx MVCC tried to abort an already committed
  non-memtx transaction if it used a system space view or tried to do
  a DDL operation (gh-10715).
