## feature/core

* Application threads are now named `<group><id>`, where `<group>` is the name
  of the configured group the thread belongs to and `<id>` is the ID of the
  thread within that group. Thread names are used in logs, so this change helps
  identify the thread that logged a particular message. Additionally, the
  meaning of the `thread_id` field returned by the `info()` function of
  the `experimental.threads` Lua module has been changed: it now shows the
  thread's ID within the group, rather than the global thread ID used
  internally (gh-12643).
