## bugfix/vinyl

* Fixed a bug when a DDL operation dropping a unique index could crash
  if performed concurrently with DML requests (gh-10094).
