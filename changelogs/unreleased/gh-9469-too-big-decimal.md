## bugfix/sql

* Fixed a crash when a decimal literal representing a decimal number greater
  than or equal to 10^38 was parsed in SQL (gh-9469).
