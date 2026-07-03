## bugfix/raft

* Fixed a bug where `box.ctl.promote()` could hang indefinitely on
  a read-only instance, even if the elections were won and the
  synchronous transaction queue was claimed successfully
  (gh-10040).
