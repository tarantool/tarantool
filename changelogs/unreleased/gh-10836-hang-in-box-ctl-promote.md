## bugfix/core

* Fixed a bug when `box.ctl.promote` hangs because a candidate server gets
  a message from a follower that the leader was already seen.
