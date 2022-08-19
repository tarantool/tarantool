## bugfix/raft

* Fixed a bug when `box.ctl.promote()` could hang and bump thousands of terms in
  a row if called on more than one node at the same time.
