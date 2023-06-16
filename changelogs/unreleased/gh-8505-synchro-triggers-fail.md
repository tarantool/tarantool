## bugfix/core

* Fixed a bug causing the `ER_CURSOR_NO_TRANSACTION` failure for transactions
  on synchronous spaces when the `on_commit/on_rollback` triggers are set
  (gh-8505).
