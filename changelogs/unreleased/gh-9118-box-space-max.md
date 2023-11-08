## bugfix/core

* Decremented the max space id (`box.schema.SPACE_MAX`). Now, the max space id
  equals 2147483646. The limit was decremented because the old value is used as
  an error indicator in the box C API. It's still possible to revert to the old
  behavior with the compatibility module option `box_space_max` (gh-9118).
