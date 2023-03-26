## bugfix/console

* Fixed `console.local_print()` accepting only string arguments. It backfired in
  some rare cases, e.g. when connecting via tarantoolctl to cartridged tarantool
  and using wrong credentials, a cdata error was passed through the
  `local_print()`, that failed to interpret it (gh-8374).
