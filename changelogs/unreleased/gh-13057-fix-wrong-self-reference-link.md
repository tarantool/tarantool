## bugfix/sql

* Fixed a bug when creating a table with a self-referencing foreign key: the
  bug occurred when the name of the table being created matched the name of
  the referenced table in a case-insensitive way (legacy name normalization)
  (gh-13057).
