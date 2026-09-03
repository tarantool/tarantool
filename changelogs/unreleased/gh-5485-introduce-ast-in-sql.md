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
* Changed the description of the error that occurs when using bind
  variables in the definition of a view, function, or trigger (gh-5485).
* Errors that were marked as syntactic errors but were not produced by
  the parser are now treated as semantic errors (gh-5485).
* Indexes for column PRIMARY KEY and UNIQUE constraints are now created before
  indexes for table PRIMARY KEY and UNIQUE constraints, which changes
  the order of automatically generated index names (gh-5485).
* SQL now builds an internal AST for SQL queries before building the VDBE.
  This affects the order of error detection - syntactic errors are now
  always detected before semantic errors (gh-5485).
