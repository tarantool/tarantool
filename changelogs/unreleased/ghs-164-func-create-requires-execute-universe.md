## bugfix/box

* **[Breaking change]** Fixed a privilege escalation where a user with only
  DDL privileges on functions could create a function with an arbitrary Lua
  body and register it as a trigger. Now creating a non-sandboxed
  function requires the `execute` privilege on `universe`, users who don't
  have this privilege have to create functions with `is_sandboxed = true`
  (ghs-164).

----

Breaking change: box: users without the `execute` privilege on `universe` can
no longer create non-sandboxed functions and have to use `is_sandboxed = true`.
