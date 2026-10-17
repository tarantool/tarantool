## bugfix/box

* Fixed `on_replace` triggers receiving the original request type when a
  `before_replace` trigger changed the operation. They now receive the type of
  the operation actually applied and written to WAL (gh-13416).
