## bugfix/memtx

* Fixed a bug when `index:count()` could return a wrong number, raise the
  last error, or fail with the `IllegalParams` error if the index has
  the `exclude_null` attribute and MVCC is enabled (gh-10396).
