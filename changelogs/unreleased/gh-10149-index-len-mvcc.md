## bugfix/memtx

* Fixed a bug when the initial index size wasn't recorded in the MVCC,
  so no transaction conflict occurred if the index size changed during
  the transaction (gh-10149).
