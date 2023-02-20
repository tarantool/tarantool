## feature/debugger

* Introduced the `-d` command-line option which runs the debugger console
  instead of the standard interactive console (gh-7456).

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

  This is a more convenient way to initiate a debugger session instead
  of an older, more invasive approach of instrumenting the code with a
  `require 'luadebug'()` call.
