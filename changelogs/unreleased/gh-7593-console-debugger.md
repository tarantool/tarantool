## feature/debugger

* Created a console debugger `luadebug.lua` for debugging of external and
  builtin Lua modules;
* New Lua API created `tarantool.debug.getsources()` which allows
  to see sources of builtin modules in any external debugger;

NB! Debugger REPL is not yet compatible with Tarantool console, i.e. this
code will hang in terminal

```lua
tarantool> dbg = require 'luadebug'
---
...

tarantool> dbg()
---
```
One should call debugger activation only in their instrumented code, not
from interactive console.
