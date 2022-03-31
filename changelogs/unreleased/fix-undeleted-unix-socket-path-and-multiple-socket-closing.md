## bugfix/core

* Fixed error related to the fact that if a user changed the listen address,
  all iproto threads closed the same socket multiple times.

* Fixed error related to Tarantool not deleting the unix socket path when
  finishing work.
