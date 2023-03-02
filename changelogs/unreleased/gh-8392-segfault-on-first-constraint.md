## bugfix/sql

* Fixed a segmentation fault if a FOREIGN KEY or CHECK constraint was declared
  before the first column during `CREATE TABLE` (gh-8392).
