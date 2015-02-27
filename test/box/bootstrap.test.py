
import sys
import yaml

server_uuid = server.get_param('server')['uuid']
sys.stdout.push_filter(server_uuid, '<server uuid>')
cluster_uuid = yaml.load(server.admin('box.space._schema:get("cluster")',
    silent = True))[0][1]
sys.stdout.push_filter(cluster_uuid, '<cluster uuid>')
sys.stdout.push_filter(server.sourcedir, '<sourcedir>')

server.admin('dofile("%s/extra/schema_erase.lua")' % server.sourcedir)
server.admin('box.space._schema:select{}')
server.admin('box.space._cluster:select{}')
server.admin('box.space._space:select{}')
server.admin('box.space._index:select{}')
server.admin('box.space._user:select{}')
server.admin('box.space._func:select{}')
server.admin('box.space._priv:select{}')

server.admin('dofile("%s/extra/schema_fill.lua")' % server.sourcedir)
server.admin("box.snapshot()")
server.restart()

server.admin('box.space._schema:select{}')
server.admin('box.space._cluster:select{}')
server.admin('box.space._space:select{}')
server.admin('box.space._index:select{}')
server.admin('box.space._user:select{}')
server.admin('box.space._func:select{}')
server.admin('box.space._priv:select{}')

# Cleanup
sys.stdout.pop_filter()
