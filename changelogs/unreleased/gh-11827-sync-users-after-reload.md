## bugfix/box

* Users and roles defined in `credentials.*` are now synchronized with the
  config on reload and instance restart: users and roles removed from config
  are dropped automatically, while manually created users and roles remain
  untouched (gh-11827).
