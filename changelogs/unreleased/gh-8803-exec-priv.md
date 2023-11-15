## feature/box

* Introduced the new `lua_eval` and `lua_call` object types for
  `box.schema.user.grant`. Granting the `'execute'` privilege on `lua_eval`
  allows the user to execute an arbitrary Lua expression with the
  `IPROTO_EVAL` request. Granting the `'execute'` privilege on `lua_call`
  allows the user to execute a global user-defined Lua function with
  the `IPROTO_CALL` request (gh-8803, gh-9360).
* **[Breaking change]** Introduced the new `sql` object type for
  `box.schema.user.grant`. Now only users with the `'execute'` privilege
  granted on `sql` or `universe` can execute SQL expressions with the
  `IPROTO_EXECUTE` or `IPROTO_PREPARE` requests. To revert to the old behavior
  (no SQL access checks), use the `sql_priv` compat option (gh-8803).
