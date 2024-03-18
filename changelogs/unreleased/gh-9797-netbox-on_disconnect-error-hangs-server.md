## bugfix/lua

* Fixed a bug in `on_disconnect` trigger of `net.box` connections which caused
  a Tarantool server to hang indefinitely when an error was thrown from the
  trigger (gh-9797).
