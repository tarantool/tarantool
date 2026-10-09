## bugfix/sql

* Fixed an out-of-bounds read when casting an empty or all-whitespace string
  to `BOOLEAN`, caused by the whitespace-trimming loops in `str_to_bool()`
  not being bounded by the remaining string length (gh-13302).
