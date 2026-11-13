## bugfix/replication

* Fixed a crash (an assertion in the debug build) when a session got a
  watcher notification or a `box.session.push()` result while the
  connection was switching to the replication mode, i.e. while a
  `JOIN`/`SUBSCRIBE`-like request was being processed on it (gh-13237).
