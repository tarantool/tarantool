## bugfix/raft

* Reconfiguration of `box.cfg.election_timeout` could lead to a crash or
  undefined behaviour if done during an ongoing election with a special WAL
  write in progress.
