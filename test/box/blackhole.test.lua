test_run = require('test_run').new()

s = box.schema.space.create('test', {engine = 'blackhole'})

-- Blackhole doesn't support indexes.
s:create_index('pk')

-- Blackhole does support space format.
s:format{{'key', 'unsigned'}, {'value', 'string'}}
s:format()
t = s:insert{1, 'a'} -- ok
t, t.key, t.value
s:insert{1, 2, 3} -- error
s:replace{'a', 'b', 'c'} -- error
s:format{}
s:insert{1, 2, 3} -- ok
s:replace{'a', 'b', 'c'} -- ok

-- Blackhole doesn't support delete/update/upsert operations.
box.internal.delete(s.id, 0, {})
box.internal.update(s.id, 0, {}, {})
box.internal.upsert(s.id, {}, {})

-- Blackhole supports on_replace and before_replace triggers.
s_old = nil
s_new = nil
f1 = s:on_replace(function(old, new) s_old = old s_new = new end)
s:replace{1, 2, 3}
s_old, s_new
f2 = s:before_replace(function(old, new) return box.tuple.new{4, 5, 6} end)
s:replace{1, 2, 3}
s_old, s_new
s:on_replace(nil, f1)
s:before_replace(nil, f2)

-- Blackhole statements can be mixed in other engines' transactions.
memtx = box.schema.space.create('memtx', {engine = 'memtx'})
_ = memtx:create_index('pk')
test_run:cmd("setopt delimiter ';'")
box.begin()
s:replace{1}
memtx:replace{1}
s:replace{2}
memtx:replace{2}
box.commit();
test_run:cmd("setopt delimiter ''");
memtx:select()
f = s:on_replace(function(old, new) memtx:replace(new) end)
s:replace{3}
s:replace{4}
memtx:select()
s:on_replace(nil, f)
memtx:drop()

-- Test recovery.
test_run:cmd('restart server default')
s = box.space.test

-- Test snapshot.
box.snapshot()

-- Operations done on a blackhole space are written to the WAL
-- and therefore get replicated. Check it with the aid of an
-- on_replace trigger.
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
t = {}
_ = box.space.test:on_replace(function(old, new) table.insert(t, new) end)
test_run:cmd('switch default')
s = box.space.test
for i = 1, 5 do s:replace{i} end
vclock = test_run:get_vclock('default')
test_run:wait_vclock('replica', vclock)
test_run:cmd("switch replica")
t
test_run:cmd('switch default')
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
box.schema.user.revoke('guest', 'replication')

s:drop()
