## feature/lua

* Lua `error.tostring()` is changed to print error stack from effect to cause
  and to add `Caused by: ` prefix for cause frames when compat option
  `box_error_serialize_verbose` is `new`.
