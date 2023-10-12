## feature/sql

* `SHOW CREATE TABLE` no longer supports the DEFAULT clause (gh-8793).
* `SQL_EXPR` functions can now be set as a default value (gh-8793).
* A literal set as the default value can no longer have a `+` or `-` sign unless
  the literal is numeric (gh-8793).
