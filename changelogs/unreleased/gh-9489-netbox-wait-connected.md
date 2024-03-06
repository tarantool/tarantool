## bugfix/lua

* Fixed a regression that caused the `wait_connected = false` option of
  `net_box.connect` to yield, despite being required to be fully asynchronous
  (gh-9489).
