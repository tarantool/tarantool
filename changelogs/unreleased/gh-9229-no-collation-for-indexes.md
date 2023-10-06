## bugfix/sql

* Fixed assertion in a debug build when a collation was added after an index
  with more than one field (gh-9229).
* Fixed a bug that in some cases would not assign collations to an index created
  during `CREATE TABLE` (gh-9229).
