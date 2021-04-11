## feature/replication

* Introduce `box.ctl.promote()` and the concept of manual elections (enabled
  with `election_mode='manual'`). Once the instance is in `manual` election
  mode, it acts like a `voter` most of the time, but may trigger elections and
  become a leader, once `box.ctl.promote()` is called.
  When `election_mode ~= 'manual'`, `box.ctl.promote()` replaces
  `box.ctl.clear_synchro_queue()`, which is now deprecated (gh-3055).
