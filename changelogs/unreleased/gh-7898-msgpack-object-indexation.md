## feature/lua/msgpack

* Added `__index` metamethod and `get` method to `msgpack.object` which perform
  indexation of MsgPack data stored in the object similar to `tuple` from
  `box.tuple`: `__index` resolves collisions in favor of
  `msgpack.object` methods, whereas `get` ignores methods (gh-7898).
