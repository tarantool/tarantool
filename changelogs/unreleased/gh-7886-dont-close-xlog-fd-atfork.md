## bugfix/core

* Fixed a bug when Tarantool could execute random bytes as a Lua code after fork
  on systems with glibc version < 2.29 (gh-7886).
