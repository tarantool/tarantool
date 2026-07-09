## feature/box

* Introduced a new range delete API. It can be used to delete a range of keys
  from an index. It is available in Lua as `index:delete_range` and in the C API
  as `box_delete_range` (gh-11335).
