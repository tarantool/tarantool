## bugfix/replication

* Fixed a possible crash on bootstrap with `box.cfg.bootstrap_strategy = 'auto'`
  when some of the bootstrapping nodes were stopped (gh-8757).
