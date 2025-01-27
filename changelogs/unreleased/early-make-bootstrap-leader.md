## feature/replication

* Allowed to call `box.ctl.make_bootstrap_leader()` before the first
  `box.cfg()` call in the `'supervised'` bootstrap strategy (gh-10858).
