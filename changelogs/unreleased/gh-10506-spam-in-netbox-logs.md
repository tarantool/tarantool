## bugfix/lua

* Fixed a bug when a lot of "Connection refused" messages were
  printed in the log file when a net_box connection fails with
  the `reconnect_after` option (gh-10506).
