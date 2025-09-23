## bugfix/replication

* Fixed two related bugs when concurrent `box.ctl.promote` invocations with
  `box.cfg.election_mode = 'manual'` would crash during (gh-11703) or after
  (gh-11708) server configuration via `box.cfg`.
