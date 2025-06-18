## bugfix/core

* Fixed a bug when `box.ctl.promote` could hang if a candidate server got
  a message from a follower that the leader was already seen (gh-10836).
