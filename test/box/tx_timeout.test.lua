test_run = require("test_run").new()
fiber = require("fiber")
test_run:cmd("create server test with script='box/tx_man.lua'")
test_run:cmd("start server test")

-- Checks for local transactions
test_run:switch("test")
fiber = require("fiber")
ffi = require("ffi")
ffi.cdef("int box_txn_set_timeout(double timeout);")

-- Check error when we try to set timeout, when
-- there is no active transaction
assert(ffi.C.box_txn_set_timeout(5) == -1)
-- No active transaction
box.error.last()

-- Check error when try to set timeout, when
-- transaction rollback timer is already running
box.begin({timeout = 100})
fiber.yield()
assert(ffi.C.box_txn_set_timeout(5) == -1)
-- Operation is not permitted if timer is already running
box.error.last()
box.commit()


-- Check arguments for 'box.begin'
box.begin(1)
box.begin({timeout = 0})
box.begin({timeout = -1})
box.begin({timeout = "5"})
-- Check new configuration option 'txn_timeout'
box.cfg({txn_timeout = 0})
box.cfg({txn_timeout = -1})
box.cfg({txn_timeout = "5"})

s = box.schema.space.create("test")
_ = s:create_index("pk")
txn_timeout = 0.1
box.cfg({ txn_timeout = txn_timeout })

-- Check that transaction aborted by timeout, which
-- was set by the change of box.cfg.txn_timeout
box.begin()
s:replace({1})
s:select({}) -- [1]
fiber.sleep(txn_timeout + 0.1)
s:select({})
s:replace({2})
fiber.yield()
s:select({})
box.commit() -- Transaction has been aborted by timeout

-- Check that transaction aborted by timeout, which
-- was set by appropriate option in box.begin
box.begin({timeout = txn_timeout})
s:replace({1})
s:select({}) -- [1]
fiber.sleep(txn_timeout  / 2 + 0.1)
s:select({})
s:replace({2})
fiber.yield()
s:select({})
box.commit() -- Transaction has been aborted by timeout

-- Check that transaction is not rollback until timeout expired.
box.begin({timeout = 1000})
s:replace({1})
s:select({1}) -- [1]
fiber.sleep(0.1)
-- timeout is not expired
s:select({}) -- [1]
box.commit() -- Success
s:select({}) -- [1]

s:drop()
test_run:switch("default")

test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
