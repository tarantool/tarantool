## bugfix/lua

* Fixed a bug that caused a `net.box` connection to remain active after being
  closed from the connection's `on_connect` trigger (gh-9827).
