test_run = require("test_run").new()
net_box = require("net.box")
fiber = require("fiber")
test_run:cmd("create server test with script='box/tx_man.lua'")
test_run:cmd("start server test")

test_run:cmd("push filter '(.builtin/.*.lua):[0-9]+' to '\\1:<line>'")

test_run:switch("test")
s = box.schema.space.create("test")
_ = s:create_index("pk")
txn_timeout = 0.5
box.cfg({ txn_timeout = txn_timeout })
box.schema.user.grant("guest", "super")
fiber = require('fiber')
function sleep_with_timeout(timeout) fiber.sleep(timeout) end
test_run:switch("default")

-- Checks for remote transactions
server_addr = test_run:eval("test", "return box.cfg.listen")[1]
txn_timeout = test_run:eval("test", "return box.cfg.txn_timeout")[1]
conn = net_box.connect(server_addr)
stream = conn:new_stream()
space = stream.space.test

-- Check invalid timeout for transaction using raw request
conn:_request('BEGIN', nil, nil, stream._stream_id, -1)

-- Check arguments for 'stream:begin'
stream:begin(1)
stream:begin({timeout = 0})
stream:begin({timeout = -1})
stream:begin({timeout = "5"})

-- Check that transaction aborted by timeout, which
-- was set by the change of box.cfg.txn_timeout on server
stream:begin()
space:replace({1})
space:select({}) -- [1]
_ = test_run:eval("test", string.format("sleep_with_timeout(%f)", txn_timeout + 0.1))
space:select({})
space:replace({2})
fiber.yield()
space:select({})
stream:commit() -- transaction was aborted by timeout

-- Check that transaction aborted by timeout, which
-- was set by appropriate option in stream:begin
stream:begin({timeout = txn_timeout})
space:replace({1})
space:select({}) -- [1]
_= test_run:eval("test", string.format("sleep_with_timeout(%f)", txn_timeout + 0.1))
space:select({})
space:replace({2})
fiber.yield()
space:select({})
stream:commit() -- transaction was aborted by timeout

-- Check that transaction is not rollback until timeout expired.
stream:begin({timeout = 1000})
space:replace({1})
space:select({}) -- [1]
fiber.sleep(0.1)
space:select({}) -- [1]
stream:commit() --Success


test_run:switch("test")
box.schema.user.revoke("guest", "super")
s:select() -- [1]
s:drop()
test_run:switch("default")

test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
