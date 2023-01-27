## feature/vinyl

* **[Breaking change]** Added support of transaction isolation levels for the
  Vinyl engine. The `txn_isolation` option passed to `box.begin()` now has the
  same effect for Vinyl and memtx. Note, this effectively changes the default
  isolation level of Vinyl transactions from 'read-committed' to 'best-effort',
  which may cause more conflicts (gh-5522).
