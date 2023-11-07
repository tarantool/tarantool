## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed error handling after return from a child coroutine.
* Fixed clashing of addresses in the `__call` metamethod return dispatch.
