## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8516). The following issues
were fixed as part of this activity:

* Fixed assembling of `IR_LREF` assembling for GC64 mode on x86_64.
