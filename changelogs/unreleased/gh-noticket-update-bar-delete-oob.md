## bugfix/core

* Fixed an out-of-bounds access in the `#` update operation applied by a JSON
  path. The number of fields to delete was not bounded by the array being
  updated, and the size of a deleted map key was assumed to be the length of
  the key in the path.
