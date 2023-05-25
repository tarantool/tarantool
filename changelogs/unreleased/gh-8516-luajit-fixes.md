## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8516). The following issues
were fixed as part of this activity:

* Fixed `IR_LREF` assembling for the GC64 mode on x86_64.
