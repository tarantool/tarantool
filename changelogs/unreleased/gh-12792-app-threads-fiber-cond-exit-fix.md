## bugfix/core

* Fixed a bug when Tarantool crashed at exit if there was a fiber waiting on
  a condition variable created in an application thread (gh-12792).
