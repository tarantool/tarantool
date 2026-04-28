## bugfix/memtx

* Fixed a bug in MVCC `read-confirmed` isolation where `delete` and `update`
  could see a wrong tuple (gh-12260).
