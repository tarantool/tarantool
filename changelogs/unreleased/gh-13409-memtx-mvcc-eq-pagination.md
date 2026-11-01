## bugfix/memtx

* Fixed a problem when MVCC tracking of pagination with an EQ or REQ
  iterator lost conflict or led to a crash (gh-13409).
