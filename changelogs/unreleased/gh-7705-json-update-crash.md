## bugfix/core

* Fixed a bug that a single JSON update couldn't insert and update a map/array
  field in 2 sequential ops. It would either crash or return an error (gh-7705).
