## feature/core

* Introduced experimental support for application threads. Like the main thread
  an application thread runs an event loop and has a Lua state with most
  built-in modules available, but it cannot access the database directly.
  Application threads are configured with the new `threads` configuration
  option. For calling other threads, including the main thread, the new Lua
  module `experimental.threads` has been added (gh-12206).
