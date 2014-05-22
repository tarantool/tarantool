import os
import sys
import struct
import socket
import msgpack
from tarantool.const import *
from tarantool import Connection

print """
#
# iproto packages test
#
"""

# opeing new connection to tarantool/box
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('localhost', server.sql.port))

print """
# Test bug #899343 (server assertion failure on incorrect packet)
"""
print "# send the package with invalid length"
invalid_request = struct.pack('<LLL', 1, 4294967290, 1)
print s.send(invalid_request)
print "# check that is server alive"
sql("ping")

# closing connection
s.close()

key_names = {}
for (k,v) in globals().items():
    if type(k) == str and k.startswith('IPROTO_') and type(v) == int:
        key_names[v] = k

def repr_dict(todump):
    d = {}
    for (k, v) in todump.items():
        k_name = key_names.get(k, k)
        d[k_name] = v
    return repr(d)

def test(header, body):
    # Connect and authenticate
    c = Connection('localhost', server.sql.port)
    c.connect()
    print 'query', repr_dict(header), repr_dict(body)
    header = msgpack.dumps(header)
    body = msgpack.dumps(body)
    query = msgpack.dumps(len(header) + len(body)) + header + body
    # Send raw request using connectred socket
    s = c._socket
    try:
        s.send(query)
    except OSError as e:
        print '   => ', 'Failed to send request'
    c.close()
    sql("ping")

print """
#  Test gh-206 "Segfault if sending IPROTO package without `KEY` field"
"""

print "IPROTO_SELECT"
test({ IPROTO_CODE : REQUEST_TYPE_SELECT }, { IPROTO_SPACE_ID: 280 })
print "\n"

print "IPROTO_DELETE"
test({ IPROTO_CODE : REQUEST_TYPE_DELETE }, { IPROTO_SPACE_ID: 280 })
print "\n"

print "IPROTO_UPDATE"
test({ IPROTO_CODE : REQUEST_TYPE_UPDATE }, { IPROTO_SPACE_ID: 280 })
test({ IPROTO_CODE : REQUEST_TYPE_UPDATE },
     { IPROTO_SPACE_ID: 280, IPROTO_KEY: (1, )})
print "\n"

print "IPROTO_REPLACE"
test({ IPROTO_CODE : REQUEST_TYPE_REPLACE }, { IPROTO_SPACE_ID: 280 })
print "\n"

print "IPROTO_CALL"
test({ IPROTO_CODE : REQUEST_TYPE_CALL }, {})
test({ IPROTO_CODE : REQUEST_TYPE_CALL }, { IPROTO_KEY: ('procname', )})
print "\n"
