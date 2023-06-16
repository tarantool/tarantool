## bugfix/core

* Fixed a bug causing the effective session and user are not propagated to
  `box.on_commit` and `box.on_rollback` trigger callbacks when transaction
  is synchronous (gh-8742).
