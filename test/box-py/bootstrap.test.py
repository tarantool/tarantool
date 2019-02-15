server.admin('box.internal.bootstrap()')
server.admin('box.space._schema:select{}')
server.admin('box.space._cluster:select{}')
server.admin('box.space._space:select{}')
server.admin('box.space._index:select{}')
server.admin('box.space._user:select{}')
server.admin('box.space._func:select{}')
server.admin('box.space._priv:select{}')

# Cleanup
server.stop()
server.cleanup()
server.deploy()
