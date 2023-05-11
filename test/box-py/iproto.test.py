from __future__ import print_function

import os
import sys
import struct
import socket
import msgpack
from tarantool.const import *
from tarantool import Connection
from tarantool.request import Request, RequestInsert, RequestSelect, RequestUpdate, RequestUpsert
from tarantool.response import Response
from lib.tarantool_connection import TarantoolConnection
from tarantool.error import DatabaseError

# FIXME: Remove after the new constants are added to the Python connector.
if not 'REQUEST_TYPE_ID' in locals():
    REQUEST_TYPE_ID = 73
    IPROTO_VERSION = 0x54
    IPROTO_FEATURES = 0x55
    IPROTO_AUTH_TYPE = 0x5b

if not 'REQUEST_TYPE_WATCH' in locals():
    REQUEST_TYPE_WATCH = 74
    REQUEST_TYPE_UNWATCH = 75
    REQUEST_TYPE_EVENT = 76
    IPROTO_EVENT_KEY = 0x57
    IPROTO_EVENT_DATA = 0x58

if not 'IPROTO_AFTER_POSITION' in locals():
    IPROTO_FETCH_POSITION = 0x1f
    IPROTO_AFTER_POSITION = 0x2e
    IPROTO_AFTER_TUPLE = 0x2f
    IPROTO_POSITION = 0x35

admin("box.schema.user.grant('guest', 'read,write,execute', 'universe')")

print("""
#
# iproto packages test
#
""")

# opening new connection to tarantool/box
conn = TarantoolConnection(server.iproto.host, server.iproto.port)
conn.connect()
s = conn.socket

print("""
# Test bug #899343 (server assertion failure on incorrect packet)
""")
print("# send the package with invalid length")
invalid_request = struct.pack("<LLL", 1, 4294967290, 1)
print(s.send(invalid_request))
print("# check that is server alive")
print(iproto.py_con.ping() > 0)

# closing connection
s.close()

# Note re IPROTO_SQL_INFO_* keys: they cannot appear in the
# response map at the top level, but have the same codes as other
# IPROTO_* constants. Exclude those names so.
key_names = {}
for (k,v) in list(globals().items()):
    if type(k) == str and k.startswith("IPROTO_") and \
            not k.startswith("IPROTO_SQL_INFO_") and type(v) == int:
        key_names[v] = k

def repr_dict(todump):
    d = {}
    for (k, v) in todump.items():
        k_name = key_names.get(k, k)
        d[k_name] = v
    return repr(sorted(d.items()))


def test(header, body):
    # Connect and authenticate
    c = Connection(None, server.iproto.port)
    c.connect()
    print("query", repr_dict(header), repr_dict(body))
    header = msgpack.dumps(header)
    body = msgpack.dumps(body)
    query = msgpack.dumps(len(header) + len(body)) + header + body
    # Send raw request using connected socket
    s = c._socket
    try:
        s.send(query)
    except OSError as e:
        print("   => ", "Failed to send request")
    c.close()
    print(iproto.py_con.ping() > 0)

print("""
#  Test gh-206 "Segfault if sending IPROTO package without `KEY` field"
""")

print("IPROTO_SELECT")
test({ IPROTO_CODE : REQUEST_TYPE_SELECT }, { IPROTO_SPACE_ID: 280 })
print("\n")

print("IPROTO_DELETE")
test({ IPROTO_CODE : REQUEST_TYPE_DELETE }, { IPROTO_SPACE_ID: 280 })
print("\n")

print("IPROTO_UPDATE")
test({ IPROTO_CODE : REQUEST_TYPE_UPDATE }, { IPROTO_SPACE_ID: 280 })
test({ IPROTO_CODE : REQUEST_TYPE_UPDATE },
     { IPROTO_SPACE_ID: 280, IPROTO_KEY: (1, )})
print("\n")

print("IPROTO_REPLACE")
test({ IPROTO_CODE : REQUEST_TYPE_REPLACE }, { IPROTO_SPACE_ID: 280 })
print("\n")

print("IPROTO_CALL")
test({ IPROTO_CODE : REQUEST_TYPE_CALL }, {})
test({ IPROTO_CODE : REQUEST_TYPE_CALL }, { IPROTO_KEY: ("procname", )})
print("\n")

# gh-434 Tarantool crashes on multiple iproto requests with WAL enabled
admin("box.cfg.wal_mode")
admin("space = box.schema.space.create('test', { id = 567 })")
admin("index = space:create_index('primary', { type = 'hash' })")
admin("box.schema.user.grant('guest', 'read,write,execute', 'space', 'test')")

c = Connection(None, server.iproto.port)
c.connect()
request1 = RequestInsert(c, 567, [1, "baobab"])
request2 = RequestInsert(c, 567, [2, "obbaba"])
s = c._socket
try:
    s.send(bytes(request1) + bytes(request2))
except OSError as e:
    print("   => ", "Failed to send request")
response1 = Response(c, c._read_response())
response2 = Response(c, c._read_response())
print(response1.__str__())
print(response2.__str__())

request1 = RequestInsert(c, 567, [3, "occama"])
request2 = RequestSelect(c, 567, 0, [1], 0, 1, 0)
s = c._socket
try:
    s.send(bytes(request1) + bytes(request2))
except OSError as e:
    print("   => ", "Failed to send request")
response1 = Response(c, c._read_response())
response2 = Response(c, c._read_response())
print(response1.__str__())
print(response2.__str__())

request1 = RequestSelect(c, 567, 0, [2], 0, 1, 0)
request2 = RequestInsert(c, 567, [4, "ockham"])
s = c._socket
try:
    s.send(bytes(request1) + bytes(request2))
except OSError as e:
    print("   => ", "Failed to send request")
response1 = Response(c, c._read_response())
response2 = Response(c, c._read_response())
print(response1.__str__())
print(response2.__str__())

request1 = RequestSelect(c, 567, 0, [1], 0, 1, 0)
request2 = RequestSelect(c, 567, 0, [2], 0, 1, 0)
s = c._socket
try:
    s.send(bytes(request1) + bytes(request2))
except OSError as e:
    print("   => ", "Failed to send request")
response1 = Response(c, c._read_response())
response2 = Response(c, c._read_response())
print(response1.__str__())
print(response2.__str__())

c.close()

admin("space:drop()")

#
# gh-522: Broken compatibility with msgpack-python for strings of size 33..255
#
admin("space = box.schema.space.create('test')")
admin("index = space:create_index('primary', { type = 'hash', parts = {1, 'string'}})")

class RawInsert(Request):
    request_type = REQUEST_TYPE_INSERT

    def __init__(self, conn, space_no, blob):
        super(RawInsert, self).__init__(conn)
        request_body = b'\x82' + msgpack.dumps(IPROTO_SPACE_ID) + \
            msgpack.dumps(space_id) + msgpack.dumps(IPROTO_TUPLE) + blob
        self._body = request_body

class RawSelect(Request):
    request_type = REQUEST_TYPE_SELECT

    def __init__(self, conn, space_no, blob):
        super(RawSelect, self).__init__(conn)
        request_body = b'\x83' + msgpack.dumps(IPROTO_SPACE_ID) + \
            msgpack.dumps(space_id) + msgpack.dumps(IPROTO_KEY) + blob + \
            msgpack.dumps(IPROTO_LIMIT) + msgpack.dumps(100);
        self._body = request_body

c = iproto.py_con
space = c.space("test")
space_id = space.space_no

TESTS = [
    (1,     b'\xa1', b'\xd9\x01', b'\xda\x00\x01', b'\xdb\x00\x00\x00\x01'),
    (31,    b'\xbf', b'\xd9\x1f', b'\xda\x00\x1f', b'\xdb\x00\x00\x00\x1f'),
    (32,    b'\xd9\x20', b'\xda\x00\x20', b'\xdb\x00\x00\x00\x20'),
    (255,   b'\xd9\xff', b'\xda\x00\xff', b'\xdb\x00\x00\x00\xff'),
    (256,   b'\xda\x01\x00', b'\xdb\x00\x00\x01\x00'),
    (65535, b'\xda\xff\xff', b'\xdb\x00\x00\xff\xff'),
    (65536, b'\xdb\x00\x01\x00\x00'),
]

for test in TESTS:
    it = iter(test)
    size = next(it)
    print("STR", size)
    print("--")
    for fmt in it:
        if sys.version[0] == "2":
            print("0x" + fmt.encode("hex"), "=>", end=" ")
        else:
            print("0x" + fmt.hex(), "=>", end=" ")
        field = "*" * size
        c._send_request(RawInsert(c, space_id, b'\x91' + fmt + field.encode("utf-8")))
        tuple = space.select(field)[0]
        print(len(tuple[0])== size and "ok" or "fail", end=" ")
        it2 = iter(test)
        next(it2)
        for fmt2 in it2:
            tuple = c._send_request(RawSelect(c, space_id,
                b'\x91' + fmt2 + field.encode("utf-8")))[0]
            print(len(tuple[0]) == size and "ok" or "fail", end=" ")
        tuple = space.delete(field)[0]
        print(len(tuple[0]) == size and "ok" or "fail", end="")
        print()
    print()


print("Test of schema_id in iproto.")
c = Connection(None, server.iproto.port)
c.connect()
s = c._socket

def receive_response():
    resp_len = ""
    resp_headerbody = ""
    resp_header = {}
    resp_body = {}
    try:
        resp_len = s.recv(5)
        resp_len = msgpack.loads(resp_len)
        resp_headerbody = s.recv(resp_len)
        # wait for the whole data
        while len(resp_headerbody) < resp_len:
            chunk = s.recv(resp_len - len(resp_headerbody))
            resp_headerbody = resp_headerbody + chunk
        unpacker = msgpack.Unpacker(use_list = True)
        unpacker.feed(resp_headerbody)
        resp_header = unpacker.unpack()
        resp_body = unpacker.unpack()
    except (OSError, socket.timeout) as e:
        print("   => ", "Failed to recv response")
    res = {}
    res["header"] = resp_header
    res["body"] = resp_body
    return res

def send_request(req_header, req_body):
    query_header = msgpack.dumps(req_header)
    query_body = msgpack.dumps(req_body)
    packet_len = len(query_header) + len(query_body)
    query = msgpack.dumps(packet_len) + query_header + query_body
    try:
        s.send(query)
    except (OSError, socket.timeout) as e:
        print("   => ", "Failed to send request")

def test_request(req_header, req_body):
    send_request(req_header, req_body)
    return receive_response()

def resp_status(resp):
    if resp["header"][IPROTO_CODE] == REQUEST_TYPE_OK:
        return "ok"
    else:
        return "error: {}".format(resp["body"][IPROTO_ERROR].decode("utf-8"))

header = { IPROTO_CODE : REQUEST_TYPE_SELECT}
body = { IPROTO_SPACE_ID: space_id,
    IPROTO_INDEX_ID: 0,
    IPROTO_KEY: [],
    IPROTO_ITERATOR: 2,
    IPROTO_OFFSET: 0,
    IPROTO_LIMIT: 1 }
resp = test_request(header, body)
print("Normal connect done w/o errors:", resp["header"][0] == 0)
print("Got schema_id:", resp["header"][5] > 0)
schema_id = resp["header"][5]

header = { IPROTO_CODE : REQUEST_TYPE_SELECT, 5 : 0 }
resp = test_request(header, body)
print("Zero-schema_id connect done w/o errors:", resp["header"][0] == 0)
print("Same schema_id:", resp["header"][5] == schema_id)

header = { IPROTO_CODE : REQUEST_TYPE_SELECT, 5 : schema_id }
resp = test_request(header, body)
print("Normal connect done w/o errors:", resp["header"][0] == 0)
print("Same schema_id:", resp["header"][5] == schema_id)

header = { IPROTO_CODE : REQUEST_TYPE_SELECT, 5 : schema_id + 1 }
resp = test_request(header, body)
print("Wrong schema_id leads to error:", resp["header"][0] != 0)
print("Same schema_id:", resp["header"][5] == schema_id)

admin("space2 = box.schema.create_space('test2')")

header = { IPROTO_CODE : REQUEST_TYPE_SELECT, 5 : schema_id }
resp = test_request(header, body)
print("Schema changed -> error:", resp["header"][0] != 0)
print("Got another schema_id:", resp["header"][5] != schema_id)

#
# gh-2334 Lost SYNC in JOIN response.
#
uuid = "0d5bd431-7f3e-4695-a5c2-82de0a9cbc95"
header = { IPROTO_CODE: REQUEST_TYPE_JOIN, IPROTO_SYNC: 2334 }
body = { IPROTO_SERVER_UUID: uuid }
resp = test_request(header, body)
if resp["header"][IPROTO_SYNC] == 2334:
    i = 1
    while i < 3:
        resp = receive_response()
        if resp["header"][IPROTO_SYNC] != 2334:
            print("Bad sync on response with number ", i)
            break
        if resp["header"][IPROTO_CODE] == REQUEST_TYPE_OK:
            i += 1
    else:
        print("Sync ok")
else:
    print("Bad first sync")

#
# Try incorrect JOIN. SYNC must be also returned.
#
body[IPROTO_SERVER_UUID] = "unknown"
resp = test_request(header, body)
if resp["header"][IPROTO_SYNC] == 2334:
    print("Sync on error is ok")
else:
    print("Sync on error is not ok")

c.close()

admin("space:drop()")
admin("space2:drop()")
admin("box.space._cluster:delete{2} ~= nil")

#
# gh-1280 Segmentation fault on space.select(tuple()) or space.select([2])
#
admin("space = box.schema.create_space('gh1280', { engine = 'vinyl' })")
admin("index = space:create_index('primary')")
admin("space:insert({1})")
admin("space:insert({2, 'Music'})")
admin("space:insert({3, 'Length', 93})")

iproto.py_con.space("gh1280").select([])
iproto.py_con.space("gh1280").select(list())


admin("space:drop()")

admin("box.schema.user.revoke('guest', 'read,write,execute', 'universe')")

#
# gh-272 if the packet was incorrect, respond with an error code
# gh-1654 do not close connnection on invalid request
#
print("""
# Test bugs gh-272, gh-1654 if the packet was incorrect, respond with
# an error code and do not close connection
""")

c = Connection(None, server.iproto.port)
c.connect()
s = c._socket
header = { "hello": "world"}
body = { "bug": 272 }
resp = test_request(header, body)
print("sync={}, {}".format(resp["header"][IPROTO_SYNC],
        resp["body"].get(IPROTO_ERROR).decode("utf-8")))
header = { IPROTO_CODE : REQUEST_TYPE_SELECT }
header[IPROTO_SYNC] = 1234
resp = test_request(header, body)
print("sync={}, {}".format(resp["header"][IPROTO_SYNC],
        resp["body"].get(IPROTO_ERROR).decode("utf-8")))
header[IPROTO_SYNC] = 5678
body = { IPROTO_SPACE_ID: 304, IPROTO_KEY: [], IPROTO_LIMIT: 1 }
resp = test_request(header, body)
print("sync={}, {}".format(resp["header"][IPROTO_SYNC],
        resp["body"].get(IPROTO_ERROR).decode("utf-8")))
c.close()


admin("space = box.schema.space.create('test_index_base', { id = 568 })")
admin("index = space:create_index('primary', { type = 'hash' })")
admin("box.schema.user.grant('guest', 'read,write,execute', 'space', 'test_index_base')")

c = Connection(None, server.iproto.port)
c.connect()
s = c._socket

request = RequestInsert(c, 568, [1, 0, 0, 0])
try:
    s.send(bytes(request))
except OSError as e:
    print("   => ", "Failed to send request")
response = Response(c, c._read_response())
print(response.__str__())

request = RequestUpdate(c, 568, 0, [1], [["+", 2, 1], ["-", 3, 1]])
try:
    s.send(bytes(request))
except OSError as e:
    print("   => ", "Failed to send request")
response = Response(c, c._read_response())
print(response.__str__())

request = RequestUpsert(c, 568, 0, [1, 0, 0, 0], [["+", 2, 1], ["-", 3, 1]])
try:
    s.send(bytes(request))
except OSError as e:
    print("   => ", "Failed to send request")
response = Response(c, c._read_response())

request = RequestSelect(c, 568, 0, [1], 0, 1, 0)
try:
    s.send(bytes(request))
except OSError as e:
    print("   => ", "Failed to send request")
response = Response(c, c._read_response())
print(response.__str__())

c.close()

#
# gh-2619 follow up: allow empty args for call/eval.
#
admin("function kek() return 'kek' end")
admin("box.schema.user.grant('guest', 'read,write,execute', 'universe')")

c = Connection(None, server.iproto.port)
c.connect()
s = c._socket

header = { IPROTO_CODE: REQUEST_TYPE_CALL, IPROTO_SYNC: 100 }
body = { IPROTO_FUNCTION_NAME: "kek" }
resp = test_request(header, body)
print("Sync: ", resp["header"][IPROTO_SYNC])
body = resp["body"][IPROTO_DATA]
if sys.version[0] == "3":
    body = [body[0].decode("utf-8")]
print("Retcode: ", body)

c.close()

admin("box.schema.user.revoke('guest', 'read,write,execute', 'universe')")

admin("space:drop()")

print("""
#
# gh-6253 IPROTO_ID
#
""")
def print_id_response(resp):
    if resp["header"][IPROTO_CODE] == REQUEST_TYPE_OK:
        print("version={}, features={}, auth_type={}".format(
            resp["body"][IPROTO_VERSION], resp["body"][IPROTO_FEATURES],
            resp["body"].get(IPROTO_AUTH_TYPE, "").decode("utf-8")))
    else:
        print(str(resp["body"][IPROTO_ERROR].decode("utf-8")))

c = Connection(None, server.iproto.port)
c.connect()
s = c._socket
header = { IPROTO_CODE: REQUEST_TYPE_ID }
print("# Invalid version")
resp = test_request(header, { IPROTO_VERSION: "abc" })
print_id_response(resp)
print("# Invalid features")
resp = test_request(header, { IPROTO_FEATURES: ["abc"] })
print_id_response(resp)
print("# Invalid auth_type")
resp = test_request(header, { IPROTO_AUTH_TYPE: 123 })
print_id_response(resp)
print("# Empty request body")
resp = test_request(header, {})
print_id_response(resp)
print("# Unknown version and features")
resp = test_request(header, { IPROTO_VERSION: 99999999,
                              IPROTO_FEATURES: [99999999] })
print_id_response(resp)
c.close()

print("""
#
# gh-6257 Watchers
#
""")
def watch(key, sync):
    print("# Watch key '{}', sync={}".format(key, sync))
    send_request({IPROTO_CODE: REQUEST_TYPE_WATCH, IPROTO_SYNC: sync},
                 {IPROTO_EVENT_KEY: key})

def unwatch(key):
    print("# Unwatch key '{}'".format(key))
    send_request({IPROTO_CODE: REQUEST_TYPE_UNWATCH}, {IPROTO_EVENT_KEY: key})

def receive_event():
    print("# Receive event")
    resp = receive_response()
    code = resp["header"].get(IPROTO_CODE)
    if code is None:
        print("<no event received>")
        return
    if code == REQUEST_TYPE_EVENT:
        print("key='{}', value={}, sync={}".format(
            resp["body"].get(IPROTO_EVENT_KEY, '').decode('utf-8'),
            resp["body"].get(IPROTO_EVENT_DATA),
            resp["header"].get(IPROTO_SYNC)))
    else:
        print("Unexpected packet: {}".format(resp))

def check_no_event():
    s.settimeout(0.01)
    receive_event()
    s.settimeout(None)

c = Connection(None, server.iproto.port)
c.connect()
s = c._socket

print("# Missing key")
resp = test_request({IPROTO_CODE: REQUEST_TYPE_WATCH}, {})
print(resp_status(resp))

print("# Invalid key type")
resp = test_request({IPROTO_CODE: REQUEST_TYPE_WATCH},
                    {IPROTO_EVENT_KEY: 123})
print(resp_status(resp))

# Register a watcher
watch("foo", 1)
receive_event()

# Register a watcher for another key
watch("bar", 2)
receive_event()

# Unregister and register watcher
unwatch("bar")
watch("bar", 3)
receive_event()

# No notification without ack
admin("box.broadcast('foo', {1, 2, 3})")
check_no_event()

# Notification after ack
watch("foo", 4)
receive_event()
watch("bar", 5)
admin("box.broadcast('bar', 123)")
receive_event()

# No notification after unregister
admin("box.broadcast('bar', 456)")
unwatch("bar")
check_no_event()

# Cleanup
c.close()
admin("box.broadcast('foo', nil)")
admin("box.broadcast('bar', nil)")

# Pagination
admin("space = box.schema.space.create('test', { id = 567 })")
admin("index = space:create_index('primary', { type = 'tree' })")
admin("box.schema.user.grant('guest', 'read,write,execute', 'space', 'test')")

c = Connection(None, server.iproto.port)
c.connect()
request = bytes()
for i in range(1, 11):
    request += bytes(RequestInsert(c, 567, [i, 1]))
s = c._socket
try:
    s.send(request)
except OSError as e:
    print("   => ", "Failed to send request")
c.close()

class RequestSelectAfter(Request):
    request_type = REQUEST_TYPE_SELECT

    def __init__(self, conn, space_id, key, fetch_pos, after_tuple, after_pos):
        super(RequestSelectAfter, self).__init__(conn)
        body = {IPROTO_SPACE_ID: space_id, IPROTO_KEY: key, IPROTO_LIMIT: 2,
                IPROTO_ITERATOR: 5, IPROTO_FETCH_POSITION: fetch_pos}
        if after_tuple:
            body[IPROTO_AFTER_TUPLE] = after_tuple
        if after_pos:
            body[IPROTO_AFTER_POSITION] = after_pos
        self._body = msgpack.dumps(body)

c = Connection(None, server.iproto.port)
c.connect()
s = c._socket
pos = ""
last_tuple = None
fetch_pos = True
space_id = 567
key = [1]
print("Simple pagination with after_pos")
for i in range(1, 7):
    request = bytes(RequestSelectAfter(c, space_id, key, fetch_pos, last_tuple, pos))
    try:
        s.send(request)
    except OSError as e:
        print("   => ", "Failed to send request")
    response = Response(c, c._read_response())
    print("Page", i)
    print(response.__str__())
    pos = response._body.get(IPROTO_POSITION, None)
print("Position after last iteration must does not present in response:", pos is None)
print("Simple pagination with after_tuple")
pos = None
fetch_pos = False
for i in range(1, 6):
    request = bytes(RequestSelectAfter(c, space_id, key, fetch_pos, last_tuple, pos))
    try:
        s.send(request)
    except OSError as e:
        print("   => ", "Failed to send request")
    response = Response(c, c._read_response())
    print("Page", i)
    print(response.__str__())
    last_tuple = response._body.get(IPROTO_DATA, None)[1]
c.close()
