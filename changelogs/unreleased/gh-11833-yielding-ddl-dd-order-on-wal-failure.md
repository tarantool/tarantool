## bugfix/core

* Fixed a crash that could happen if two DDL operations (index build or space
  format change) were executed on the same space and a WAL write error occurred
  (gh-11833).
