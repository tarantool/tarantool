## bugfix/core

* Fixed a crash that could occur when an application handled a signal after
  shutting down Tarantool's fiber runtime. The runtime now restores the
  thread's previous alternate signal stack during shutdown (gh-13808).
