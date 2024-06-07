## bugfix/vinyl

* Fixed a bug when an `upsert` statement crashed in case the created tuple had
  fields conflicting with the primary key definition (gh-10099).
