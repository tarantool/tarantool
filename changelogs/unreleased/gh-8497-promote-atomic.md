## bugfix/replication

* Fixed a possible failure to promote the desired node by `box.ctl.promote()` on
  a cluster with nodes configured with `election_mode = "candidate"` (gh-8497).
