msgpack = require('msgpack')
socket = require('socket')
urilib = require('uri')

IPROTO_REQUEST_TYPE = 0x00
IPROTO_PREPARE      = 13
IPROTO_SQL_TEXT     = 0x40
IPROTO_STMT_ID      = 0x43

box.schema.user.grant('guest', 'read, write, execute', 'universe')
uri = urilib.parse(box.cfg.listen)
s = socket.tcp_connect(uri.host, uri.service)
_ = s:read(128) -- skip greeting

header = { [IPROTO_REQUEST_TYPE] = IPROTO_PREPARE }
body = { [IPROTO_SQL_TEXT] = 'SELECT 1' }
response = iproto_request(s, header, body)

body = { [IPROTO_STMT_ID] = response['body'][IPROTO_STMT_ID] }
-- Decoding of the response will fail if there's no body.
response = iproto_request(s, header, body)
response.body

box.schema.user.revoke('guest', 'read, write, execute', 'universe')
s:close()

