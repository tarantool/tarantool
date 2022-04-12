## bugfix/core

* Fixed usage of `box.session.peer()` in `box.session.on_disconnect()` trigger.
  Now, it's safe to assume that `box.session.peer()` returns the address of the
  disconnected peer, not nil, as it used to (gh-7014).
