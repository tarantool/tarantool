## bugfix/core

* Fixed a possible buffer overflow in `mp_decode_decimal()` and
  `decimal_unpack()` when an input string was too long (ghs-17).
