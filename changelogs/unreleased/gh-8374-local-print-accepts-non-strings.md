## bugfix/console

* Fixed `console.local_print()` failing on non-string arguments, which led to
  some rare errors. For example, when connecting via tarantoolctl to cartridged
  tarantool with incorrect credentials, a cdata error was passed through the
  `local_print()`, which failed to interpret it (gh-8374).
