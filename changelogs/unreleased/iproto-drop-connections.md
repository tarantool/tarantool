## feature/box

* Introduced a new API for closing iproto connections. It can be used to either
  close iproto connections, or only those that correspond to URIs that are
  not being listened. It is available in Lua as `box.iproto.drop_connections`.
