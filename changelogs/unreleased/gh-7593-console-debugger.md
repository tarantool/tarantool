## feature/debugger

* Introduced a new console debugger `luadebug.lua` for debugging external and
  builtin Lua modules.

> Note: the debugger REPL is not yet compatible with Tarantool console.
> This means that this code will hang in the console:
>
>```lua
>tarantool> dbg = require 'luadebug'
>---
>...
>
>tarantool> dbg()
>---
>```
> Users should call debugger activation only in their instrumented code, not
> from the interactive console.

* Introduced a new Lua API `tarantool.debug.getsources()` which allows
  seeing sources of builtin modules in any external debugger.
