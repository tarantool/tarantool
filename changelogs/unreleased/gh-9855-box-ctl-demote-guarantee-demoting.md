## bugfix/replication

* Fixed a bug that allowed the old leader in
  `box.cfg{election_mode = 'candidate'` mode to get re-elected after resigning
  himself through `box.ctl.demote` (gh-9855).
