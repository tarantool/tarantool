## feature/box

* Hide redundant fields from `box.error.unpack()` if
  the `box_error_unpack_type_and_code` compat option is set to 'new'.
  The default behaviour is 'old' (gh-9101).
