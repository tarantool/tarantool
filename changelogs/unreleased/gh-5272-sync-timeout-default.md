## feature/replication

* Added a compat option "box_cfg_replication_sync_timeout" to control the
  default value of "replication_sync_timeout" box.cfg option. Old default is 300
  seconds and new default is 0. The noticeable difference in new behavior is
  that `box.cfg{replication = ""}` call returns before the node is synced with
  remote instances. If one expects the node to be writable once `box.cfg`
  returns, this can be achieved with new behavior by calling `box.ctl.wait_rw()`
  after `box.cfg{replication=...}`.

  By default the option is "old" and will be switched to "new" in the next major
  release. If you wish to try out the new behaviour, you have to set the option
  to "new" before the initial `box.cfg{}` call in your application (gh-5272).
