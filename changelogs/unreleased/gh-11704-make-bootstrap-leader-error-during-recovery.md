## bugfix/replication

* Fixed a false-positive assertion failure that could occur when calling
  `box.ctl.make_bootstrap_leader()` during recovery (gh-11704).
