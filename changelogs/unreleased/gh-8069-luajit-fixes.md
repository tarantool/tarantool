## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-8069). In the scope of this
activity, the following issues have been resolved:

* Fixed `emit_rma()` for x64/GC64 mode for non-`mov` instructions.
* Limited Lua C library path with the default `PATH_MAX` value of 4096 bytes.
