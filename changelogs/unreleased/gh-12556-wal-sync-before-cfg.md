## bugfix/core

* Fixed a crash on calling `box.ctl.wal_sync()` before `box.cfg{}`.
  Now it raises an `ER_UNCONFIGURED` error in this case (gh-12556).
