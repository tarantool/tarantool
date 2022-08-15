## bugfix/raft

* Fixed a bug when a node with `election_mode='voter'` could hang in
  `box.ctl.promote()` or even become a leader.
