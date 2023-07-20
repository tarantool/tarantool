## feature/box

* Introduced the new `lua_eval` and `lua_call` object types for
  `box.schema.user.grant`. Granting the `'execute'` privilege on `lua_eval`
  allows the user to execute an arbitrary Lua expression with the
  `IPROTO_EVAL` request. Granting the `'execute'` privilege on `lua_call`
  allows the user to execute any global user-defined Lua function with
  the `IPROTO_CALL` request (gh-8803).
