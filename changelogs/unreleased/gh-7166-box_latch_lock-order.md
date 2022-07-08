## bugfix/core

* Now `box_latch_lock` guarantees the order in which it is acquired by
  fibers, which requested it (gh-7166).
