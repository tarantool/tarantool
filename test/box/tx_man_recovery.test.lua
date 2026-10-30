env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man_recovery with script='box/tx_man.lua'")
test_run:cmd("start server tx_man_recovery")
test_run:cmd("switch tx_man_recovery")

_ = box.schema.space.create('test', { engine = 'memtx' })
_ = box.space.test:create_index('primary')

tx = require('txn_proxy').new()
tx:begin()
tx('box.space.test:replace{1}')
box.snapshot()
test_run:cmd("restart server tx_man_recovery")
box.space.test:select{}

box.space.test:replace{2, 1}
tx = require('txn_proxy').new()
tx:begin()
tx('box.space.test:replace{2, 2}')
box.snapshot()
test_run:cmd("restart server tx_man_recovery")
box.space.test:select{}

tx = require('txn_proxy').new()
tx:begin()
tx('box.space.test:replace{2, 3}')
box.snapshot()
tx:commit()
test_run:cmd("restart server tx_man_recovery")
box.space.test:select{}

tx = require('txn_proxy').new()
tx:begin()
tx('box.space.test:replace{2, 4}')
box.snapshot()
tx:rollback()
test_run:cmd("restart server tx_man_recovery")
box.space.test:select{}

test_run:cmd("switch default")
test_run:cmd("stop server tx_man_recovery")
test_run:cmd("cleanup server tx_man_recovery")
