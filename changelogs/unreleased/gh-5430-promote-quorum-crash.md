## bugfix/replication

* Fixed a possible crash when `box.ctl.promote()` was called in a cluster with
  >= 3 instances, happened in debug build. In release build it could lead to
  undefined behaviour. It was likely to happen if a new node was added shortly
  before the promotion (gh-5430).
