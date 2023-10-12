## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* LJ_GC64: Always snapshot functions for non-base frames.
