## bugfix/core

* Fixed `box.cfg.listen` accepting oversized Unix socket paths and listening at
  a truncated path. Tarantool now reports `Unix socket path is too long`
  (gh-13808).
