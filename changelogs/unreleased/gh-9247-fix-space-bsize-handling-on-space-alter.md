## bugfix/core

* Fixed a bug that could result in the incorrect `space:bsize()` when altering
  a primary index concurrently with DML operations (gh-9247).
