test_run = require('test_run').new()
test_run:cmd('restart server default')
net = require('net.box')

--
-- gh-3958 updating box.cfg.readahead doesn't affect existing connections.
--
readahead = box.cfg.readahead

box.cfg{readahead = 128}

s = box.schema.space.create("test")
_ = s:create_index("pk")
box.schema.user.grant("guest", "read,write", "space", "test")

-- connection is created with small readahead value,
-- make sure it is updated if box.cfg.readahead is changed.
c = net.connect(box.cfg.listen)

box.cfg{readahead = 100 * 1024}

box.error.injection.set("ERRINJ_WAL_DELAY", true)
pad = string.rep('x', 8192)
for i = 1, 5 do c.space.test:replace({i, pad}, {is_async = true}) end
box.error.injection.set("ERRINJ_WAL_DELAY", false)

test_run:wait_log('default', 'readahead limit is reached', 1024, 0.1)

s:drop()
box.cfg{readahead = readahead}
