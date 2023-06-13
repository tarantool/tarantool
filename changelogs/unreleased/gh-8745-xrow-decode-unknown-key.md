## bugfix/core

* Fixed a bug when Tarantool failed to decode a request containing an unknown
  IPROTO key. The bug resulted in broken connectivity between Tarantool 2.10
  and 2.11 (gh-8745).
