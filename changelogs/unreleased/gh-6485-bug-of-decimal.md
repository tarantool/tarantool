## bugfix/sql

* Fixed truncation of `DECIMAL` during implicit cast to `INTEGER` in `LIMIT`
  and `OFFSET`.

* Fixed truncation of `DECIMAL` during implicit cast to `INTEGER` when value is
  used in an index.

* Fixed assert on a cast of `DECIMAL` value that is greater than -1.0 and less
  than 0.0 to `INTEGER` (gh-6485).
