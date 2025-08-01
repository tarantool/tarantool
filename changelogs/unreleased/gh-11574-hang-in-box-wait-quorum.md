## bugfix/core

* Bug fixed: Reducing replication_synchro_quorum didn't
  affect the hang of `box.ctl.promote` during transition
  to RW state. (gh-11574).
