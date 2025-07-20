## bugfix/memtx

* Fixed a Memtx-MVCC bug when a transaction performing get-after-replace could
  dirty-read nothing (gh-11687).
