## bugfix/replication

* Fixed a bug when `box.ctl.demote()` with `box.cfg{election_mode = 'off'}`
  and an owned synchro queue could simply not do anything (gh-6860).
