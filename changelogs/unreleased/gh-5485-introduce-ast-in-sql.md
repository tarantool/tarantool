## feature/sql

* Some invalid `ALTER TABLE ADD COLUMN`/`ADD CONSTRAINT` statements now fail
  with a plain syntax error instead of a "keyword is reserved" error (gh-5485).
