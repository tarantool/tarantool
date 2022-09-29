## bugfix/core

* Fixed a bug when a single JSON update couldn't insert and update a field of a map or
  an array in two sequential calls. It would either crash or return an error (gh-7705).
