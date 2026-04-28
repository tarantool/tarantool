## bugfix/memtx

* Fixed a bug in MVCC `read-confirmed` isolation where `delete` and `update`
  could see an incorrect tuple (gh-12260).
