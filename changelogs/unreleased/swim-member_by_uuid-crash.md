## bugfix/swim

* Fix the crash on an attempt to call `swim:member_by_uuid()` with no arguments
  or with `nil`/`box.NULL` (gh-5951).
