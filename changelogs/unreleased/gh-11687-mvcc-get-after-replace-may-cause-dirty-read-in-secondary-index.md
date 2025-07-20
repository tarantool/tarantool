## bugfix/mvcc

* Fixed an MVCC bug when a transaction performing get-after-replace could
  dirty-read nothing.
