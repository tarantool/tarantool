## bugfix/core

* Fixed a bug when direct assignments of `box.cfg` parameters (such as
  `box.cfg.background = true`) were silently ignored. Now such assignments
  result in errors. The correct way to set `box.cfg` parameters is this:
  `box.cfg{ background=true }` (gh-7350).
