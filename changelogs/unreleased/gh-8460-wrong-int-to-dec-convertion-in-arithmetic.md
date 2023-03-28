## bugfix/sql

* Fixed incorrect conversion of an integer greater than `INT64_MAX` or
  less than `0` to a decimal number during SQL arithmetic operations (gh-8460).
