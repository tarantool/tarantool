## bugfix/vinyl

* Fixed a bug in the tuple cache when a tuple could become inaccessible via
  a secondary index after a transaction rollback caused by a WAL write error
  (gh-11140).
