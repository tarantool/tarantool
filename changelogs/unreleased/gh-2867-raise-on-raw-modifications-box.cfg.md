## bugfix/core

* Fixed an incorrect behaviour when assigning a value to parameter in `box.cfg`
  changes nothing. Fix raises an error on change `box.cfg` parameters using
  standard Lua table interface: `box.cfg.background = true` (gh-7350).
