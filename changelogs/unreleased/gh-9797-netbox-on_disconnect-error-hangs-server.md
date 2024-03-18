## bugfix/lua

* Fixed a bug in the `on_disconnect` trigger of `net.box` connections that
  caused Tarantool server to hang indefinitely when an error was thrown from the
  trigger (gh-9797).
