## feature/box

* Introduced a new range delete API. It can be used to delete a range of keys
  from an index. It's available in Lua as `index:delete_range` and in C API as
  `box_delete_range` (gh-11335).
