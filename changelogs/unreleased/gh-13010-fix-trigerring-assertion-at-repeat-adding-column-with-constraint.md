## bugfix/sql

* Fixed a bug where the foreign field ID for a field foreign key
  was lost after executing an `ALTER TABLE ADD COLUMN` statement (gh-13010).
