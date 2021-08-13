## bugfix/lua

* Fixed Lua C API misuse, when the error is raised during call results encoding
  on unprotected coroutine and expected to be catched on the different one, that
  is protected (gh-6248).
