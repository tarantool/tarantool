env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

txn_proxy = require('txn_proxy')

tx1 = txn_proxy.new()
tx2 = txn_proxy.new()
tx3 = txn_proxy.new()

s = box.schema.space.create('test')

i = s:create_index('pk', {parts={{1, 'uint'}}})

-- Completely empty selects
tx1:begin()
tx2:begin()
tx1('s:select{}')
tx2('s:select{}')
tx1('s:replace{1, 1}')
tx2('s:replace{2, 2}')
tx1:commit()
tx2:commit()

s:truncate()

-- Non-empty selects
s:replace{3, 3}

tx1:begin()
tx2:begin()
tx1('s:select{}')
tx2('s:select{}')
tx1('s:replace{1, 1}')
tx2('s:replace{2, 2}')
tx1:commit()
tx2:commit()

s:truncate()

-- Insert then delete
s:replace{3, 3}
s:delete{3}

tx1:begin()
tx2:begin()
tx1('s:select{}')
tx2('s:select{}')
tx1('s:replace{1, 1}')
tx2('s:replace{2, 2}')
tx1:commit()
tx2:commit()

s:truncate()

-- GT: non-intersect
tx1:begin()
tx2:begin()
tx1('s:select({5}, {iterator=\'GT\'})')
tx2('s:select({5}, {iterator=\'GT\'})')
tx1('s:replace{1, 1}')
tx2('s:replace{2, 2}')
tx1:commit()
tx2:commit()

s:select{}

s:truncate()

-- GT: non-intersect, borders (1)
tx1:begin()
tx2:begin()
tx1('s:select({5}, {iterator=\'GT\'})')
tx2('s:replace{5, 1}')
tx1:commit()
tx2:commit()

s:select{}

s:truncate()

-- GT: non-intersect, borders (2)
tx1:begin()
tx2:begin()
tx1('s:select({5}, {iterator=\'GT\'})')
tx2('s:select({6}, {iterator=\'GT\'})')
tx2('s:replace{5, 1}')
tx1('s:replace{6, 2}')
tx1:commit()
tx2:commit()

s:truncate()

-- GE: sending to read-view
tx1:begin()
tx2:begin()
tx1('s:select({5}, {iterator=\'GE\'})')
tx2('s:replace{5, 2}')
tx2:commit()
tx1('s:select({5}, {iterator=\'GE\'})')
tx1:commit()

s:truncate()

-- GE: sending to read-view, different order
tx1:begin()
tx2:begin()
tx1('s:replace{5, 1}')
tx2('s:select{5}')
tx1:commit()
tx2('s:select{5}')
tx2:commit()

s:truncate()

-- Empty point-like select
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
tx1:commit()
tx2:commit()

s:truncate()

-- Empty point-like select: no intersection
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{2}')
tx1('s:replace{1, 1}')
tx2('s:replace{2, 2}')
tx1:commit()
tx2:commit()

s:truncate()

s:drop()

test_run:cmd("switch default")
test_run:cmd("stop server tx_man")
test_run:cmd("cleanup server tx_man")
