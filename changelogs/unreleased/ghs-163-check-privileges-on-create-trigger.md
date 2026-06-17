## bugfix/sql

* Added permission checks when creating a trigger in SQL.
  Now only users with 'alter' privileges on a table can create
  triggers for that table (ghs-163).
