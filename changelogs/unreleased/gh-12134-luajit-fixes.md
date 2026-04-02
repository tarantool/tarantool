## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-12134). The following
issues were fixed as part of this activity:

* Added `ffi.abi("dualnum")`.
* Fixed stack checks in varargs calls in the GC64 build.
* Fixed stack checks in `pcall()`/`xpcall()` in the GC64 build.
* Fixed the allocation limit for the no-JIT build.
* Fixed handling of OOM errors on stack resizing in `coroutine.resume()` and
  `lua_checkstack()`.
* Fixed recording of loops with a `-0` `step` value or `NaN` control values.
* Fixed error reporting when an error occurs during error handling.
* Fixed a dangling reference for FFI callbacks.
* Fixed `BC_UNM` for a `-0` argument in the dual-number mode.
* Fixed narrowing of unary minus in the dual-number mode.
* Fixed recording of `string.byte()`, `string.sub()`, and `string.find()`.
* Fixed missing type conversion for `BC_FORI` slots in the dual-number mode.
* Fixed various corner cases in VM events.
* Fixed constructor index resolution recording in the JIT compiler.
* Fixed a UBSan warning in `unpack()`.
