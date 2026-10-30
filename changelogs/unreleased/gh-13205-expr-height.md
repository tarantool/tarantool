## bugfix/sql

* Fixed a bug where an expression with a `COLLATE` clause could exceed
  the expression tree depth limit without an error being raised (gh-13205).
* Fixed a crash on a long chain of binary operators or `COLLATE` clauses
  in an SQL expression (gh-13205).
