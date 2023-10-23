## bugfix/core

* Fixed the behavior of `log.cfg{modules = ...}`. Now, instead of merging the
  new log modules configuration with the old one, it completely overwrites the
  current configuration, which is consistent with `box.cfg{log_modules = ...}`
  (gh-7962).
