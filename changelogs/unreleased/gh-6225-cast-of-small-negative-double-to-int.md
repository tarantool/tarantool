## bugfix/sql

* Fixed assert on a cast of `DOUBLE` value greater than -1.0 and less than 0.0
  to `INTEGER` and `UNSIGNED` (gh-6255).
