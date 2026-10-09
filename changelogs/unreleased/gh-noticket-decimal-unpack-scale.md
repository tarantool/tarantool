## bugfix/core

* Fixed decoding of a MsgPack decimal whose scale does not fit in `int32_t`.
  The scale was truncated instead of rejected, so such a value was accepted
  as a different number.
