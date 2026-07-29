## feature/sql

* Some invalid `ALTER TABLE ADD COLUMN`/`ADD CONSTRAINT` statements now fail
  with a plain syntax error instead of a "keyword is reserved" error (gh-5485).
* Qualified `db.table` names in trigger `INSERT`/`UPDATE`/`DELETE` bodies now
  fail with a generic syntax error instead of a special error (gh-5485).
