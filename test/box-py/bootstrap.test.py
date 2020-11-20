server.admin("box.internal.bootstrap()")
server.admin("box.space._schema:select{}")
server.admin("box.space._cluster:select{}")
server.admin("box.space._space:select{}")
server.admin("box.space._index:select{}")
server.admin("box.space._user:select{}")
server.admin("for _, v in box.space._func:pairs{} do r = {} table.insert(r, v:update({{\"=\", 18, \"\"}, {\"=\", 19, \"\"}})) return r end")
server.admin("box.space._priv:select{}")

# Cleanup
server.stop()
server.cleanup()
server.deploy()
