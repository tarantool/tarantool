env = require('test_run')
net_box = require('net.box')
test_run = env.new()

test_run:cmd("create server test with script='box/gh-5860-implement-streams.lua'")

-- Some simple checks for new object - stream
test_run:cmd("start server test with args='1'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]
-- User can use automatically generated stream_id or sets it
-- manually, not mix this.
conn = net_box.connect(server_addr)
stream = conn:stream()
-- Error unable to mix user and automatically generated stream_id
-- for one connection.
_ = conn:stream(1)
conn:close()
conn = net_box.connect(server_addr)
stream = conn:stream(1)
-- Error unable to mix user and automatically generated stream_id
-- for one connection.
_ = conn:stream()
conn:close()
-- For different connections it's ok
conn_1 = net_box.connect(server_addr)
stream_1 = conn_1:stream(1)
conn_2 = net_box.connect(server_addr)
stream_2 = conn_2:stream()
-- Stream is a wrapper around connection, so if you close connection
-- you close stream, and vice versa.
conn_1:close()
assert(not stream_1:ping())
stream_2:close()
assert(not conn_2:ping())
-- Simple checks for transactions
conn_1 = net_box.connect(server_addr)
conn_2 = net_box.connect(server_addr)
stream_1_1 = conn_1:stream(1)
stream_1_2 = conn_1:stream(2)
-- It's ok to have streams with same id for different connections
stream_2 = conn_2:stream(1)
-- It's ok to commit or rollback without any active transaction
stream_1_1:commit()
stream_1_1:rollback()

stream_1_1:begin()
-- Error unable to start second transaction in one stream
stream_1_1:begin()
-- It's ok to start transaction in separate stream in one connection
stream_1_2:begin()
-- It's ok to start transaction in separate stream in other connection
stream_2:begin()
test_run:cmd("switch test")
-- It's ok to start local transaction separately with active stream
-- transactions
box.begin()
box.commit()
test_run:cmd("switch default")
stream_1_1:commit()
stream_1_2:commit()
stream_2:commit()

-- Second argument (false is a value for memtx_use_mvcc_engine option)
-- Server start without active transaction manager, so all transaction
-- fails because of yeild!
test_run:cmd("start server test with args='1, false'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]

test_run:cmd("switch test")
s = box.schema.space.create('test', { engine = 'memtx' })
_ = s:create_index('primary')
-- function ping() return "pong" end //TODO
test_run:cmd('switch default')

conn = net_box.connect(server_addr)
assert(conn:ping())
stream = conn:stream()
-- Get stream id, it's used in replace/select/insert/upsert and other
-- functions to specify stream_id for request.
stream_id = stream:stream()
space = conn.space.test

-- Check syncronious stream txn requests for memtx
-- with memtx_use_mvcc_engine = false
stream:begin()
space:replace({1}, { stream_id = stream_id } )
-- Empty select, transaction was not commited and
-- is not visible from requests not belonging to the
-- transaction.
space:select{}
-- Select is empty, because memtx_use_mvcc_engine is false
space:select( {}, { stream_id = stream_id })
test_run:cmd("switch test")
-- Select is empty, transaction was not commited
s:select()
test_run:cmd('switch default')
-- Commit fails, transaction yeild with memtx_use_mvcc_engine = false
stream:commit()
-- Select is empty, transaction was aborted
space:select{}
-- Check that after failed transaction commit we able to start next
-- transaction (it's strange check, but it's necessary because it was
-- bug with it)
stream:begin()
stream:ping()
-- stream:call('ping') -- TODO
stream:commit()
test_run:cmd('switch test')
s:drop()
test_run:cmd('switch default')
test_run:cmd("stop server test")

-- Next we check transactions only for memtx with
-- memtx_use_mvcc_engine = true and for vinyl, because
-- if memtx_use_mvcc_engine = false all transactions fails,
-- as we can see before!

-- Second argument (true is a value for memtx_use_mvcc_engine option)
-- Same test case as previous but server start with active transaction
-- manager. Also check vinyl, because it's behaviour is same.
test_run:cmd("start server test with args='1, true'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]

test_run:cmd("switch test")
s1 = box.schema.space.create('test_1', { engine = 'memtx' })
s2 = box.schema.space.create('test_2', { engine = 'vinyl' })
_ = s1:create_index('primary')
_ = s2:create_index('primary')
test_run:cmd('switch default')

conn = net_box.connect(server_addr)
assert(conn:ping())
stream_1 = conn:stream()
stream_id1 = stream_1:stream()
stream_2 = conn:stream()
stream_id2 = stream_2:stream()
space_1 = conn.space.test_1
space_2 = conn.space.test_2
-- Check syncronious stream txn requests for memtx
-- with memtx_use_mvcc_engine = true and to vinyl:
-- behaviour is same!
stream_1:begin()
space_1:replace({1}, { stream_id = stream_id1 } )
stream_2:begin()
space_2:replace({1}, { stream_id = stream_id2 } )
-- Empty select, transaction was not commited and
-- is not visible from requests not belonging to the
-- transaction.
space_1:select{}
space_2:select{}
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space_1:select( {}, { stream_id = stream_id1 })
space_2:select( {}, { stream_id = stream_id2 })
test_run:cmd("switch test")
-- Select is empty, transaction was not commited
s1:select()
s2:select()
test_run:cmd('switch default')
-- Commit was successful, transaction can yeild with
-- memtx_use_mvcc_engine = true. Vinyl transactions
-- can yeild also.
stream_1:commit()
stream_2:commit()
-- Select return tuple, which was previously inserted,
-- because transaction was successful
space_1:select{}
space_2:select{}
test_run:cmd("switch test")
-- Select return tuple, which was previously inserted,
-- because transaction was successful
s1:select()
s2:select()
test_run:cmd('switch test')
s1:drop()
s2:drop()
test_run:cmd('switch default')
test_run:cmd("stop server test")

-- Check conflict resolution in stream transactions,
test_run:cmd("start server test with args='1, true'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]

test_run:cmd("switch test")
s1 = box.schema.space.create('test_1', { engine = 'memtx' })
_ = s1:create_index('primary')
s2 = box.schema.space.create('test_2', { engine = 'vinyl' })
_ = s2:create_index('primary')
test_run:cmd('switch default')

conn = net_box.connect(server_addr)
space_1 = conn.space.test_1
space_2 = conn.space.test_2
stream_1 = conn:stream()
stream_id1 = stream_1:stream()
stream_2 = conn:stream()
stream_id2 = stream_2:stream()
stream_1:begin()
stream_2:begin()

-- Simple read/write conflict.
space_1:select({1}, { stream_id = stream_id1 } )
space_1:select({1}, { stream_id = stream_id2 })
space_1:replace({1, 1}, { stream_id = stream_id1 } )
space_1:replace({1, 2}, { stream_id = stream_id2 })
stream_1:commit()
-- This transaction fails, because of conflict
stream_2:commit()
-- Here we must accept [1, 1]
space_1:select({}, { stream_id = stream_id1 } )

-- Same test for vinyl sapce
stream_1:begin()
stream_2:begin()
space_2:select({1}, { stream_id = stream_id1 } )
space_2:select({1}, { stream_id = stream_id2 })
space_2:replace({1, 1}, { stream_id = stream_id1 } )
space_2:replace({1, 2}, { stream_id = stream_id2 })
stream_1:commit()
-- This transaction fails, because of conflict
stream_2:commit()
-- Here we must accept [1, 1]
space_2:select({}, { stream_id = stream_id1 } )
test_run:cmd('switch test')
-- Both select return tuple [1, 1], transaction commited
s1:select()
s2:select()
s1:drop()
s2:drop()
test_run:cmd('switch default')
test_run:cmd("stop server test")

-- Check rollback as a command for memtx and vinyl spaces
test_run:cmd("start server test with args='1, true'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]

test_run:cmd("switch test")
s1 = box.schema.space.create('test_1', { engine = 'memtx' })
_ = s1:create_index('primary')
s2 = box.schema.space.create('test_2', { engine = 'vinyl' })
_ = s2:create_index('primary')
test_run:cmd('switch default')

conn = net_box.connect(server_addr)
space_1 = conn.space.test_1
space_2 = conn.space.test_2
stream_1 = conn:stream()
stream_id1 = stream_1:stream()
stream_2 = conn:stream()
stream_id2 = stream_2:stream()
stream_1:begin()
stream_2:begin()

-- Test rollback for memtx space
space_1:replace({1}, { stream_id = stream_id1 } )
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space_1:select({}, { stream_id = stream_id1 } )
stream_1:rollback()
-- Select is empty, transaction rollback
space_1:select({}, { stream_id = stream_id1 } )

-- Test rollback for vinyl space
space_2:replace({1}, { stream_id = stream_id2 } )
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space_2:select({}, { stream_id = stream_id2 } )
stream_2:rollback()
-- Select is empty, transaction rollback
space_2:select({}, { stream_id = stream_id2 } )

-- This is simple test is necessary because i have a bug
-- with halting stream after rollback
stream_1:begin()
stream_1:commit()
stream_2:begin()
stream_2:commit()
conn:close()

test_run:cmd('switch test')
-- Both select are empty, because transaction rollback
s1:select()
s2:select()
s1:drop()
s2:drop()
test_run:cmd('switch default')
test_run:cmd("stop server test")

-- Check rollback on disconnect
test_run:cmd("start server test with args='1, true'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]

test_run:cmd("switch test")
s1 = box.schema.space.create('test_1', { engine = 'memtx' })
_ = s1:create_index('primary')
s2 = box.schema.space.create('test_2', { engine = 'vinyl' })
_ = s2:create_index('primary')
test_run:cmd('switch default')

conn = net_box.connect(server_addr)
space_1 = conn.space.test_1
space_2 = conn.space.test_2
stream_1 = conn:stream(1)
stream_id1 = stream_1:stream()
stream_2 = conn:stream(2)
stream_id2 = stream_2:stream()
stream_1:begin()
stream_2:begin()

space_1:replace({1}, { stream_id = stream_id1 } )
space_1:replace({2}, { stream_id = stream_id1 } )
-- Select return two previously inserted tuples
space_1:select({}, { stream_id = stream_id1 } )

space_2:replace({1}, { stream_id = stream_id2 } )
space_2:replace({2}, { stream_id = stream_id2 } )
-- Select return two previously inserted tuples
space_2:select({}, { stream_id = stream_id2 } )
conn:close()

-- Reconnect
conn = net_box.connect(server_addr)
space_1 = conn.space.test_1
space_2 = conn.space.test_2
stream_1 = conn:stream(1)
stream_id1 = stream_1:stream()
stream_2 = conn:stream(2)
stream_id2 = stream_2:stream()
-- We can begin new transactions with same stream_id, because
-- previous one was rollbacked and destroyed.
stream_1:begin()
stream_2:begin()
-- Two empty selects
space_1:select({}, { stream_id = stream_id1 } )
space_2:select({}, { stream_id = stream_id2 } )
stream_1:commit()
stream_2:commit()

test_run:cmd('switch test')
-- Both select are empty, because transaction rollback
s1:select()
s2:select()
s1:drop()
s2:drop()
test_run:cmd('switch default')
test_run:cmd("stop server test")

