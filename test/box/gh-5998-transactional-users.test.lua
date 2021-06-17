env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

txn_proxy = require('txn_proxy')
map = {x = 3}

-- Firstly, let's test that in scope of one transaction all
-- invocations to users are trully transactional.
--
tx1 = txn_proxy.new()

-- Insert-Delete-Commit
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1:commit()

box.space._user:select({35})

-- Insert-Delete-Rollback
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1:rollback()

box.space._user:select({35})
box.space._user:insert({35, 1, 'asd', 'user', map})

-- Delete-Insert-Rollback
tx1:begin()
tx1("box.space._user:delete({35})")
tx1("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx1:rollback()

box.space._user:select({35})

-- Delete-Insert-Commit
tx1:begin()
tx1("box.space._user:delete({35})")
tx1("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx1:commit()

box.space._user:select({35})
box.space._user:delete({35})

-- Insert-Replace-Rollback
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:replace({35, 1, 'dsa', 'user', map})")
tx1:rollback()

box.space._user:select({35})

-- Insert-Replace-Commit
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:replace({35, 1, 'dsa', 'user', map})")
tx1:commit()

box.space._user:select({35})

-- Replace-Delete-Rollback
tx1:begin()
tx1("box.space._user:replace({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1:rollback()

box.space._user:select({35})

-- Replace-Delete-Commit
tx1:begin()
tx1("box.space._user:replace({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1:commit()

box.space._user:select({35})

-- Insert-Delete-Insert-Commit
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx1:commit()

box.space._user:select({35})
box.space._user:delete({35})

-- Insert-Delete-Insert-Rollback
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx1:rollback()

box.space._user:select({35})

-- Insert-Delete-Insert-Commit
tx1:begin()
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx1("box.space._user:delete({35})")
tx1("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx1:commit()

box.space._user:select({35})
box.space._user:delete({35})

-- Then let's test that several transactions can operate on
-- _user at the same time.
--
tx2 = txn_proxy.new()

tx1:begin()
tx2:begin()

-- Parallel insert.
--
tx1("box.space._user:insert({35, 1, 'asd', 'user', map})")
tx2("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx2("box.space._user:insert({35, 1, 'dsa', 'user', map})")
tx2("box.space._user:select({35})")
tx1("box.space._user:select({35})")

tx1:commit()
tx2:commit()

box.space._user:delete({35})

box.space._user:select({35})
box.space._user:delete({35})

-- Original test case which led to crash due to wrong on_rollback trigger.
--
tx1:begin()
tx2:begin()

tx1("box.schema.user.create('internal1')")
tx2("box.schema.user.create('internal2')")

tx1:commit()
tx2:commit()

box.schema.user.drop('internal1')

-- Still lock works only on write operations on _user. Let's setup user and
-- read its entry from cache while checking access on any write operation --
-- this will work (however intuitively it shouldn't).
--
box.schema.user.create('test')
box.schema.user.grant('test', 'write', 'space', '_space')

tx1:begin()
tx2:begin()
tx1("box.schema.user.revoke('test', 'write', 'space', '_space')")
tx2("box.session.su('test')")
tx2("box.schema.create_space('t1')")

tx1:rollback()
tx2:rollback()

box.schema.user.drop('test')

-- Check that write operations on _user blocks other txs to read modified user.
--
tx1:begin()
tx2:begin()
tx1("box.schema.user.create('test')")
tx2("box.session.su('test')")
tx2("box.schema.user.grant('test', 'write', 'space', '_space')")

tx1:rollback()
tx2:rollback()

test_run:cmd("switch default")
test_run:cmd("stop server tx_man")
test_run:cmd("cleanup server tx_man")
