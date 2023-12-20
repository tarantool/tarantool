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
