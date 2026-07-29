## feature/sql

* Some invalid `ALTER TABLE ADD COLUMN`/`ADD CONSTRAINT` statements now fail
  with a plain syntax error instead of a "keyword is reserved" error (gh-5485).
* Qualified `db.table` names in trigger `INSERT`/`UPDATE`/`DELETE` bodies now
  fail with a generic syntax error instead of a special error (gh-5485).
* `JOIN` type errors changed: combining `INNER` with `OUTER` now reports
  "JOIN cannot be both OUTER and INNER"; an unrecognized `JOIN` keyword is
  now a syntax error instead of the 'unknown or unsupported join type'
  error (gh-5485).
* The SQL query used to create a view is now in the corresponding space
  definition in `_space` as it was provided, without any changes (gh-5485).
* The SQL query used to create a trigger is now in the corresponding SQL trigger
  definition in `_trigger` as it was provided, without any changes (gh-5485).
