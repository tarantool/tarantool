## bugfix/build

* Fixed libcurl configuring, when tarantool itself is configured with `cmake3`
  command and there is no `cmake` command in PATH (gh-5955).

  This affects building tarantool from sources with bundled libcurl (it is the
  default mode).
