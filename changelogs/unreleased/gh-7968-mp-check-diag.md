## feature/lua/msgpack

* Improved error reporting for `msgpack.decode`. Now, an error raised by
  `mgpack.decode` has a detailed error message and the offset in the input
  data. If `msgpack.decode` failed to unpack a MsgPack extension, it also
  includes the error cause pointing to the error in the extension data
  (gh-7986).
