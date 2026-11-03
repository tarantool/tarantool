## bugfix/replication

* Fixed a bug where a newly bootstrapped leader set `box.info.status` to
  `waiting_for_own_rows` and remained read-only because of a connected
  instance from another replica set (gh-13657).
