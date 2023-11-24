## feature/core

* Now it is possible to set IPROTO request handler callbacks by using the new
  Lua module `trigger`, for example:
  ```lua
  local trigger = require('trigger')
  trigger.set('box.iproto.override.select', 'my_select', my_select_handler)
  ```
  The method works before the initial `box.cfg{}` call. Also, the method allows
  setting multiple handlers for a single request type (gh-8138).
