## feature/replication

* A new `compat` option `box_cfg_replication_sync_timeout` was added to
  control the default value of `replication_sync_timeout` option of `box.cfg`.
  The old default is 300 seconds, and new default is 0. The noticeable difference
  in the new behavior is that `box.cfg{replication = ""}` call now returns
  before the node is synced with remote instances. If you need the node to be
  writable once `box.cfg` returns, you can achieve it with new behavior by
  calling `box.ctl.wait_rw()` after `box.cfg{replication=...}`.

  By default, the option value is "old" and will be switched to "new" in the
  next major release. To switch to the new behavior, set the option to "new"
  **before** the initial `box.cfg{}` call in your application (gh-5272).
