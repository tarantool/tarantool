## feature/config

* Added a warning when a user or role is granted the `execute` permission
  without the `lua_call` privilege, since it implicitly allows calling
  permissive global functions (i.e. `box.session.su()`) (gh-12830).
