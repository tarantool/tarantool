## bugfix/box

* Fixed a bug in `space_object:create_index()` when `collation` option is not
  set. Now it is inherited from the space format (gh-5104).
