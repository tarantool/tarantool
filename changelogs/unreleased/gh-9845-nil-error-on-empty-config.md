## bugfix/config

* Fixed a non-verbose error on an empty configuration file.
  Now Tarantool can successfully start up with an empty configuration
  file using data from other configuration sources (gh-9845).
