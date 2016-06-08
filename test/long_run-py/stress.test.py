import os
import sys
import re
import yaml
from lib.tarantool_server import TarantoolServer

server = TarantoolServer(server.ini)
server.script = 'long_run-py/lua/stress.lua'
server.vardir = os.path.join(server.vardir, 'stress')
server.deploy()
server.stop()
print('First pass')
server.deploy()
server.stop()
print('Second pass')

