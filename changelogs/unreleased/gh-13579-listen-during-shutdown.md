## bugfix/core

* Fixed a bug when `box.cfg{}` with an unchanged `listen` option called
  during the graceful shutdown (for example, by the config module) could
  make iproto accept new connections again (gh-13579).
