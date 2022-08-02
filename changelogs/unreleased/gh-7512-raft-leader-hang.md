## bugfix/replication

* Fixed followers with `box.cfg.election_mode` turned on not noticing leader
  hang due to some long request without yields, like a `select{}` from a large
  space or `pairs` iteration without yields between loop cycles (gh-7512).
