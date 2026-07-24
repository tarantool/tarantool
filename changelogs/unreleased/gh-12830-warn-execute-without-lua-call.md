## feature/config

* Added a warning when a user or role is granted the `execute` permission on
  the universe without a `lua_call` restriction, since it allows calling all
  global functions, including `box.session.su()` (gh-12830).
