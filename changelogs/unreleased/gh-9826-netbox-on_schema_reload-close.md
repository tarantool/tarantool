## bugfix/lua

* Fixed a bug that caused a `net.box` connection to crash after being closed
  from the connection's `on_schema_reload` trigger (gh-9621).
