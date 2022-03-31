## bugfix/sql

* The `HEX()` SQL built-in function no longer throws an assert when its
  argument consists of zero-bytes (gh-6113).
