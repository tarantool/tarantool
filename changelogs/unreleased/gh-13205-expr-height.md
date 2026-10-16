## bugfix/sql

* Fixed a bug where an expression with a `COLLATE` clause could exceed
  the expression tree depth limit without an error (gh-13205).
* Fixed a bug where the height of a subquery was counted twice towards
  the expression tree depth limit, so a valid query with a subquery could
  fail with an error (gh-13205).
