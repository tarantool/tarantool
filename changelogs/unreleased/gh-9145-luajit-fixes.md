## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed CSE of a `REF_BASE` operand across `IR_RETF`.
* Fixed the fold rule for `BUFHDR APPEND`.
* Fixed HREFK, ALOAD, HLOAD, forwarding vs. `table.clear()`.
* Fixed snapshot PC when linking to `BC_JLOOP` that was a `BC_RET*`.
* Fixed dangling references to CType.
* Ensured returned string is alive in `ffi.typeinfo()`.
* Fixed the missing initialization of the internal structure, leading to a
  crash when recording a trace with an allocation of cdata.
* Limited exponent range in number parsing by `2^20`.
* Fixed double-emitting of `IR_NEWREF` when restoring sunk values for side
  trace (gh-7937).
* Fixed the `IR_HREFK` optimization for huge tables.
* Fixed recording of the `__concat` metamethod.
* Fixed the embedded bytecode loader.
* Improved error reporting on stack overflow.
* Fixed assertion on the Lua stack overflow for a stitched trace.
* Fixed snapshoting of functions for non-base frames.
