## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8516). The following issues
were fixed as part of this activity:

* Fix canonicalization of +-0.0 keys for IR_NEWREF.
