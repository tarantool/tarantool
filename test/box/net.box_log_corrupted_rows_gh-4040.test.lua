test_run = require('test_run').new()
socket = require('socket');

LISTEN = require('uri').parse(box.cfg.listen)

test_run:cmd('create server connecter with script = "box/proxy.lua"')

--
-- related to gh-4040: log corrupted rows
--
log_level = box.cfg.log_level
box.cfg{log_level=6}
sock = socket.tcp_connect(LISTEN.host, LISTEN.service)
sock:read(9)
-- we need to have a packet with correctly encoded length,
-- so that it bypasses iproto length check, but cannot be
-- decoded in xrow_header_decode
-- 0x3C = 60, sha1 digest is 20 bytes long
data = string.fromhex('3C'..string.rep(require('digest').sha1_hex('bcde'), 3))
sock:write(data)
sock:close()

test_run:wait_log('default', 'Got a corrupted row.*', nil, 10)
test_run:wait_log('default', '00000000:.*', nil, 10)
test_run:wait_log('default', '00000010:.*', nil, 10)
test_run:wait_log('default', '00000020:.*', nil, 10)
test_run:wait_log('default', '00000030:.*', nil, 10)
-- we expect nothing below, so don't wait
test_run:grep_log('default', '00000040:.*')

box.cfg{log_level=log_level}
