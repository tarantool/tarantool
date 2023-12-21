## bugfix/config

* Start the interactive console before the first `box.cfg()` call. This allows
  you to issue the `box.ctl.make_bootstrap_leader()` command to a replica set
  that uses the `supervised` bootstrap strategy (gh-8862).
