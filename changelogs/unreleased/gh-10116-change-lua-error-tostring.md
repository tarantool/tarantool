## feature/lua

* Changed Lua `error.tostring()` to print error stack from effect to cause
  and add the `Caused by: ` prefix for cause frames when the compat option
  `box_error_serialize_verbose` is set to `new`.
