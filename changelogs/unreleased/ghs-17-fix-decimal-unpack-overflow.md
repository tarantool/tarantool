## bugfix/core

* Fixed a possible buffer overflow in `mp_decode_decimal()` and
  `decimal_unpack()` when an input string is too long (ghs-17).
