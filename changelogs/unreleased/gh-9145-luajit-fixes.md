## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Limited exponent range in number parsing by `2^20`.
* Fixed build with internal unwinding.
* Fixed double-emitting of `IR_NEWREF` when restoring sunk values for side
  trace (gh-7937).
* Fixed the `IR_HREFK` optimization for huge tables.
* Fixed recording of the `__concat` metamethod.
* Fixed the embedded bytecode loader.
* Improved error reporting on stack overflow.
* Fixed assertion on the Lua stack overflow for a stitched trace.
* Fixed snapshoting of functions for non-base frames.
