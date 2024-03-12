## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-10199). The
following issues were fixed as part of this activity:

* FFI: Turn FFI finalizer table into a proper GC root.
* FFI: Treat cdata finalizer table as a GC root.
