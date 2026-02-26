## bugfix/replication

* Fixed a bug where a replica on subscribe could get stuck in the `sync` state
  for the duration of `replication.timeout`. This issue was observable in
  `box.info.replication[...].upstream` and could lead to temporary
  inconveniences, such as the inability to run `box.ctl.promote()` (when
  `election_mode` was not `off`) (gh-12025).
