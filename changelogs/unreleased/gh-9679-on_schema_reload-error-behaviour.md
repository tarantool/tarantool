## bugfix/lua

* The `on_schema_reload` trigger behavior of `net.box` connections when an
  error is thrown is now consistent with the behavior of the `on_disconnect`
  trigger (gh-9679).
