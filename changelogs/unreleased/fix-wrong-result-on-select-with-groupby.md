## bugfix/sql

* Fix wrong result of SELECT with GROUP BY in case one of selected values is
  VARBINARY, which is not directly obtained from a space (gh-5890).
