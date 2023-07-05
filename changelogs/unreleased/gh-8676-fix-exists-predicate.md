## bugfix/sql

* The `EXISTS` predicate no longer requires `LIMIT 1` to work correctly if more
  than one row is returned in the subselect (gh-8676).
