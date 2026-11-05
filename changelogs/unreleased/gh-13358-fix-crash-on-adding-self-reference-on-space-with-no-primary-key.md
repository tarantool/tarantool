## bugfix/sql

* Fixed a crash on `ALTER TABLE ADD COLUMN` with a foreign key referencing
  the same space when the space had no primary key (gh-13358).
