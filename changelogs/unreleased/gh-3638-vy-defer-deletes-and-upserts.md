## bugfix/vinyl

* Fixed a bug in Vinyl because of which the Vinyl garbage collector (compactor)
  could skip stale tuples stored in a secondary index in case the secondary
  index was the first secondary index created for the space and upsert
  operations were used on the space before the secondary index was created
  (gh-3638).
