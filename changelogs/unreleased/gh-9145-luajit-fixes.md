## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed dangling references to CType.
* Ensured returned string is alive in `ffi.typeinfo()`.
* Fixed the missing initialization of the internal structure, leading to a
  crash when recording a trace with an allocation of cdata.
