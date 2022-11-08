## feature/debugger

* Introduced command-line option which runs debugger console
  instead of standard interactive console (gh-7456).

```sh
$ tarantool -d debug-target.lua
Tarantool debugger 2.11.0-entrypoint-852-g9e6ed28ae
type 'help' for interactive help
luadebug: Loaded for 2.11.0-entrypoint-852-g9e6ed28ae
break via debug-target.lua => debug-target.lua:1 in chunk at debug-target.lua:0
   1 => local date = require 'datetime'
   4
luadebug>
```
This is more convenient way to initiate debugger session, instead
of an older, more invasive approach of instrumenting code with
`require 'luadebug'()` call.
