## bugfix/vinyl

* Fixed a bug in the vinyl garbage collector. It could skip stale tuples stored
  in a secondary index if upsert operations were used on the space before the index
  was created (gh-3638).
