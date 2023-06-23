## feature/lua

* **[Breaking change]** Added the new `varbinary` type to Lua. An object of
  this type is similar to a plain string but encoded in MsgPack as `MP_BIN` so
  it can be used for storing binary blobs in the database. This also works the
  other way round: data fields stored as `MP_BIN` are now decoded in Lua as
  varbinary objects, not as plain strings, as they used to be. Since the latter
  may cause compatibility issues, the new compat option `binary_data_decoding`
  was introduced to revert the built-in decoder to the old behavior (gh-1629).
