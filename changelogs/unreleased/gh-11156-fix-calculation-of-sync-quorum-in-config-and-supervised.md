## bugfix/replication

* Fixed a bug when the node configured as
  `box.cfg.bootstrap_mode` = `'config'`/`'supervised''` didn't switch to
  the 'orphan' status even if it failed to synchronize with each of the
  connected nodes. (gh-11156).
