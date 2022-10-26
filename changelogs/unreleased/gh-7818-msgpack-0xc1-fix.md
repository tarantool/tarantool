## bugfix/lua

* Fixed a crash in `msgpack.decode` in case the input string contains invalid
  MsgPack header `0xc1` (gh-7818).
