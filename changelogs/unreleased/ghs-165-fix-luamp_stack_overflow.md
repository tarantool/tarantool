## bugfix/msgpack

* Fixed a stack overflow when unpacking MsgPack input with deeply
  nested values containing arrays and maps
  ([ghs-165](https://github.com/tarantool/security/issues/165)).
