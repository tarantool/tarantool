## feature/replication

* Implemented correct recovery of mixed transactions. To do this, set
  `box.cfg.force_recovery` to `true`. If you need to revert to the old
  behavior, don't set the `force_recovery` option (gh-7932).
