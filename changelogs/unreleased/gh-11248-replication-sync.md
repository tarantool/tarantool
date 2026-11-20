## feature/replication

* Added `box.ctl.replication_sync({timeout, include_anon})` to wait for every
  registered replica, and optionally connected anonymous replicas, to
  acknowledge the vclock captured at the start of the call (gh-11248).
