## bugfix/box

* Added a check that disables setting `box.cfg` and `log.cfg` options to
  infinite numbers (NaN, Inf). Setting a `box.cfg` or `log.cfg` option to
  an infinite number could result in a crash or invalid behavior (gh-4962).
