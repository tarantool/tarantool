## feature/core/fiber

* Now the `channel:close()` closes the channel gracefully: closing it
  for writing leaving the possibility to read existing events from it.
  Previously, `channel:close()` was closing the channel completely and
  discarding all unread events.

  A new `compat` option `fiber_channel_close_mode` is added for switching to
  the new behavior (gh-7746).
