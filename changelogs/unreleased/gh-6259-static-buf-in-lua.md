## bugfix/core

 * `box.info.uuid`, `box.info.cluster.uuid`, and `tostring(decimal)` with any
   decimal number in Lua sometimes could return garbage if `__gc` handlers are
   used in user's code (gh-6259).
