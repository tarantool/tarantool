## feature/replication

* You may now control which node new replicas choose as a bootstrap leader
  without touching node config. To do so, set `box.cfg.bootstrap_strategy` to
  `'supervised'`, and the nodes will only bootstrap off the node on which you
  called `box.ctl.make_bootstrap_leader()` last.
  This works on an empty replica set bootstrap as well: start the admin console
  before configuring the nodes. Then configure the nodes:
  ```lua
  box.cfg{
      bootstrap_strategy = 'supervised',
      replication = ...,
      listen = ...,
  }
  ```
  Finally, call `box.ctl.make_bootstrap_leader()` through the admin console
  on the node you want to promote. All the nodes will bootstrap off that node
  (gh-8509).
