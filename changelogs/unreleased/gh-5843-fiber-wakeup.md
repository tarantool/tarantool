## bugfix/core

* Remove assertion raised in case of `fiber_wakeup()` get called with
  dead fibers. Due to backward compatibility we've allowed such calls
  for release builds but not for debug builds. In result there
  was inconsistency between program behaviour (gh-5843).
