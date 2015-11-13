from lib.box_connection import BoxConnection
from lib.tarantool_connection import TarantoolConnection
from tarantool import NetworkError
from tarantool.const import IPROTO_GREETING_SIZE, IPROTO_CODE, IPROTO_ERROR, \
    REQUEST_TYPE_ERROR
import socket
import msgpack

print """
 #
 # if on_connect() trigger raises an exception, the connection is dropped
 #
 """

# silence possible error of strict mode
server.admin("nosuchfunction = nil")
server.admin("function f1() nosuchfunction() end")
server.admin("type(box.session.on_connect(f1))")

unpacker = msgpack.Unpacker(use_list = False)

conn = TarantoolConnection(server.iproto.host, server.iproto.port)
conn.connect()
s = conn.socket

# Read greeting
print 'greeting: ', len(s.recv(IPROTO_GREETING_SIZE)) == IPROTO_GREETING_SIZE

# Read error packet
IPROTO_FIXHEADER_SIZE = 5
fixheader = s.recv(IPROTO_FIXHEADER_SIZE)
print 'fixheader: ', len(fixheader) == IPROTO_FIXHEADER_SIZE
unpacker.feed(fixheader)
packet_len = unpacker.unpack()
packet = s.recv(packet_len)
unpacker.feed(packet)

# Parse packet
header = unpacker.unpack()
body = unpacker.unpack()
print 'error code', (header[IPROTO_CODE] & (REQUEST_TYPE_ERROR - 1))
print 'error message: ', body[IPROTO_ERROR]
print 'eof:', len(s.recv(1024)) == 0
s.close()

server.admin("box.session.on_connect(nil, f1)")
