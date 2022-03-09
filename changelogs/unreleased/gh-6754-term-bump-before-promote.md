## bugfix/raft

* Fixed race condition between tx and relay when calling box.ctl.promote()
  or box.ctl.demote() (gh-6754).
