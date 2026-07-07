## bugfix/core

* Fixed a crash on calling `box.snapshot()` during initial database
  configuration. Now it raises the `ER_UNCONFIGURED` error in this case
  (gh-12879).
