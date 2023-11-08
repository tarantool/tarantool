## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed error handling after return from a child coroutine.
* Fixed buffer overflow in parsing the `#pragma` directive via FFI (gh-9339).
  Now the error is thrown when more than 6 alignment settings are pushed on the
  internal stack.
