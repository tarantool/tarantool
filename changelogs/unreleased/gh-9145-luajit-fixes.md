## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed error handling after return from a child coroutine.
* Fixed clashing of addresses in the `__call` metamethod return dispatch (again).
* Fixed the assembling for the head of the side trace (gh-8767).
* Prevented compilation of `__concat` methamethod with tailcall to fast
  function.
* Fixed buffer overflow in parsing the `#pragma` directive via FFI (gh-9339).
  Now the error is thrown when more than 6 alignment settings are pushed on the
  internal stack.
* Fixed incorrect fold rule for `x - (-0)` on trace (for `x = -0` the result
  should be `0`).
* Fixed output for `IR_CONV` in `jit.dump()`.
* Fixed `math.min()`/`math.max()` inconsistencies for x86/x86_64 architectures
  when called with a NaN argument or `-0` and `0`.
* Fixed `math.ceil(x)` result sign for -1 < x < 0.5.
* Errors from gc finalizers are now printed instead of being rethrown.
* Fixed `lua_concat()`.
* Fixed possible storing of NaN keys to table on trace.
* Fixed ABC FOLD optimization with constants.
* Marked `CONV` as non-weak, to prevent invalid control flow path choice.
* Fixed CSE of a `REF_BASE` operand across `IR_RETF`.
* Fixed the fold rule for `BUFHDR APPEND`.
* Fixed HREFK, ALOAD, HLOAD, forwarding vs. `table.clear()`.
* Fixed snapshot PC when linking to `BC_JLOOP` that was a `BC_RET*`.
* Fixed dangling references to CType.
* Ensured returned string is alive in `ffi.typeinfo()`.
* Fixed the missing initialization of the internal structure, leading to a
  crash when recording a trace with an allocation of cdata.
