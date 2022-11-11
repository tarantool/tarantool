## bugfix/core

* Fixed a bug in the MsgPack library that could lead to a failure to detect
  invalid MsgPack input and, as a result, an out-of-bounds read (ghs-18).
