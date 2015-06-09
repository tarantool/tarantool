import os
import sys
import re
import yaml
from lib.tarantool_server import TarantoolServer

server = TarantoolServer(server.ini)
server.script = 'long_run/lua/finalizers.lua'
server.vardir = os.path.join(server.vardir, 'finalizers')
try:
    server.deploy()
except:
    print "Expected error:", sys.exc_info()[0]
else:
    print "Error! exception did not occur"


