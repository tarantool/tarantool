## bugfix/core

* Fixed a bug when `on_rollback` trigger functions were invoked with an empty
  iterator argument if a transaction was aborted by a fiber yield or by a
  timeout (gh-9340).
