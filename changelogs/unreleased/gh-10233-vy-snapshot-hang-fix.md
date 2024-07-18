## bugfix/vinyl

* Fixed a bug when a race between `box.snapshot` and the creation of a new
  index could lead to a fiber hang (gh-10233, gh-10267).
