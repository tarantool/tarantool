## bugfix/lua

* Made `on_schema_reload` trigger behaviour of `net.box` connections when an
  error is thrown consistent with behaviour of `on_disconnect` trigger
  (gh-9679).
