## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9595). The following
issues were fixed as part of this activity:

* No side traces are recorded now after disabling the JIT via `jit.off()`.
