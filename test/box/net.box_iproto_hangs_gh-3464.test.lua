msgpack = require 'msgpack'
test_run = require('test_run').new()
net = require('net.box')

--
-- gh-3464: iproto hangs in 100% CPU when too big packet size
-- is received due to size_t overflow.
--
c = net:connect(box.cfg.listen)
data = msgpack.encode(18400000000000000000)..'aaaaaaa'
c._transport.perform_request(nil, nil, false, 'inject', nil, nil, nil, data)
c:close()
test_run:grep_log('default', 'too big packet size in the header') ~= nil
