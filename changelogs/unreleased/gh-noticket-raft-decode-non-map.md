## bugfix/replication

* Fixed an out-of-bounds read on an `IPROTO_RAFT` request whose body is
  valid MsgPack but not a map. Such a body is now rejected while the
  request is decoded.
