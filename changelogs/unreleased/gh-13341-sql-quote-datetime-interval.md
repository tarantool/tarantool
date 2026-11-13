## bugfix/sql

* Fixed a bug where the `QUOTE()` function ignored its `DATETIME` or
  `INTERVAL` argument and built the result from stale contents of the
  result register (gh-13341).
