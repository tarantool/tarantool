## feature/sql

* Added permission checks when dropping a trigger in SQL.
  Now only users with 'alter' privileges on a table can drop
  triggers for that table (ghs-163).
