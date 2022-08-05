## bugfix/replication

* Fixed a bug when followers with `box.cfg.election_mode` turned on did not notice
  the leader hang due to a long request, such as a `select{}` from a large
  space or a `pairs` iteration without yields between loop cycles (gh-7512).
