## bugfix/raft

* Fixed a bug where `box.cfg()` of a freshly bootstrapped instance with
  `election_mode` set to `'candidate'` or `'manual'` could sometimes return
  with the instance being in a read-only state for some time afterwards, even
  when no read-only mode was explicitly configured (gh-12972).
