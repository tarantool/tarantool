## bugfix/vinyl

* Fixed a bug when a tuple was not returned by range `select`. The bug could
  also trigger a crash in the read iterator (gh-10109).
