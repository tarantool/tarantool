## bugfix/iproto

* Fixed a bug where an oversized packet length caused a large input buffer
  allocation before the packet size was validated when no payload bytes were
  received ([ghs-177](https://github.com/tarantool/security/issues/177)).
