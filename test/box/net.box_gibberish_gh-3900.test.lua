test_run = require('test_run').new()
LISTEN = require('uri').parse(box.cfg.listen)

--
-- gh-3900: tarantool can be crashed by sending gibberish to a
-- binary socket
--
socket = require("socket")
sock = socket.tcp_connect(LISTEN.host, LISTEN.service)
data = string.fromhex("6783000000000000000000000000000000000000000000800000C8000000000000000000000000000000000000000000FFFF210100373208000000FFFF000055AAEB66486472530D02000000000010A0350001008000001000000000000000000000000000D05700")
sock:write(data)
test_run:wait_log('default', 'E> Invalid MsgPack %- packet body', nil, 10)
sock:close()
