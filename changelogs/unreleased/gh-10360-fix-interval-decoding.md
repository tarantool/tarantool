## bugfix/core

* Fixed reading after MsgPack end of invalid interval MsgPack on decoding.
  Fixed checking bounds on decoding of year, month, week, and nanosecond, and
  adjusted the fields of the interval MsgPack (gh-10360).
