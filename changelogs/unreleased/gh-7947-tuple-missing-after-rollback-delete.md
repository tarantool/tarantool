## bugfix/vinyl

* Fixed a bug that could result in `select()` skipping an existing tuple after
  a rolled back `delete()` (gh-7947).
