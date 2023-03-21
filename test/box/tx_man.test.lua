env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

txn_proxy = require('txn_proxy')
tx1 = txn_proxy.new()
tx2 = txn_proxy.new()
tx3 = txn_proxy.new()

-- https://github.com/tarantool/tarantool/issues/6274
-- This test is very short, but fragile : it depends on amount of garbage
-- in both memtx index's GC and transactional manager's GC.
-- That's why it is placed in the beginning of the file - without this commit
-- it would fail every time.
sp = box.schema.space.create('test')
_ = sp:create_index('test1', {parts={1}})
_ = sp:create_index('test2', {parts={2}})
sp:replace{1, 1}
sp:delete{1}
sp:drop()
collectgarbage()

s = box.schema.space.create('test')
i1 = s:create_index('pk', {parts={{1, 'uint'}}})
i2 = s:create_index('sec', {parts={{2, 'uint'}}})

s2 = box.schema.space.create('test2')
i21 = s2:create_index('pk', {parts={{1, 'uint'}}})
i22 = s2:create_index('sec', {parts={{2, 'uint'}}})

-- Simple read/write conflicts.
s:replace{1, 0}
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
tx1:commit()
tx2:commit()
s:select{}

-- Simple read/write conflicts, different order.
s:replace{1, 0}
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
tx2:commit() -- note that tx2 commits first.
tx1:commit()
s:select{}

-- Insert read/write conflicts.
s:delete{1}
tx1:begin()
tx2:begin()
tx1('s:insert{1, 1}')
tx2('s:insert{1, 2}')
tx1:commit()
tx2:commit()
s:select{}

-- Insert read/write conflicts, different order.
s:delete{1}
tx1:begin()
tx2:begin()
tx1('s:insert{1, 1}')
tx2('s:insert{1, 2}')
tx2:commit() -- note that tx2 commits first.
tx1:commit()
s:select{}

-- Implicit read/write conflicts.
s:replace{1, 0}
tx1:begin()
tx2:begin()
tx1("s:update({1}, {{'+', 2, 3}})")
tx2("s:update({1}, {{'+', 2, 5}})")
tx1:commit()
tx2:commit()
s:select{}

-- Implicit read/write conflicts, different order.
s:replace{1, 0}
tx1:begin()
tx2:begin()
tx1("s:update({1}, {{'+', 2, 3}})")
tx2("s:update({1}, {{'+', 2, 5}})")
tx2:commit() -- note that tx2 commits first.
tx1:commit()
s:select{}
s:delete{1}

-- Conflict in secondary index.
tx1:begin()
tx2:begin()
tx1("s:replace{1, 1}")
tx2("s:replace{2, 1}")
tx1:commit()
tx2:commit()
s:select{}
s:delete{1}

-- Conflict in secondary index, different order.
tx1:begin()
tx2:begin()
tx1("s:replace{1, 2}")
tx2("s:replace{2, 2}")
tx2:commit() -- note that tx2 commits first.
tx1:commit()
s:select{}
s:delete{2}

-- TXN is send to read view.
s:replace{1, 1}
s:replace{2, 2}
s:replace{3, 3}
tx1:begin()
tx2:begin()

tx1("s:select{}")
tx2("s:replace{1, 11}")
tx2("s:replace{2, 12}")
tx2:commit()
tx1("s:select{}")
tx1:commit()

s:delete{1}
s:delete{2}
s:delete{3}

-- TXN is send to read view but tries to replace and becomes conflicted.
s:replace{1, 1}
s:replace{2, 2}
s:replace{3, 3}
tx1:begin()
tx2:begin()

tx1("s:select{}")
tx2("s:replace{1, 11}")
tx2("s:replace{2, 12}")
tx2:commit()
tx1("s:select{}")
tx1("s:replace{3, 13}")
tx1("s:select{}")
tx1:commit()

s:delete{1}
s:delete{2}
s:delete{3}

-- Use two indexes
s:replace{1, 3}
s:replace{2, 2}
s:replace{3, 1}

tx1:begin()
tx2:begin()
tx1("i2:select{}")
tx2("i2:select{}")
tx1("s:replace{2, 4}")
tx1("i2:select{}")
tx2("i2:select{}")
tx1("s:delete{1}")
tx1("i2:select{}")
tx2("i2:select{}")
tx1:commit()
tx2("i2:select{}")
tx2:commit()
i2:select{}

s:delete{2}
s:delete{3}

-- More than two spaces
s:replace{1, 1}
s:replace{2, 2}
s2:replace{1, 2}
s2:replace{2, 1}
tx1:begin()
tx2:begin()
tx1("s:replace{3, 3}")
tx2("s2:replace{4, 4}")
tx1("s:select{}")
tx1("s2:select{}")
tx2("s:select{}")
tx2("s2:select{}")
tx1:commit()
tx2:commit()
s:select{}
s2:select{}
s:truncate()
s2:truncate()

-- Rollback
s:replace{1, 1}
s:replace{2, 2}
tx1:begin()
tx2:begin()
tx1("s:replace{4, 4}")
tx1("s:replace{1, 3}")
tx2("s:replace{3, 3}")
tx2("s:replace{1, 4}")
tx1("s:select{}")
tx2("s:select{}")
tx1:rollback()
tx2:commit()
s:select{}
s:truncate()

-- Delete the same value
s:replace{1, 1}
s:replace{2, 2}
s:replace{3, 3}
tx1:begin()
tx2:begin()
tx1("s:delete{2}")
tx1("s:select{}")
tx2("s:select{}")
tx2("s:delete{2}")
tx1("s:select{}")
tx2("s:select{}")
tx1:commit()
tx2:commit()
s:select{}
s:truncate()

-- Delete and rollback the same value
s:replace{1, 1}
s:replace{2, 2}
s:replace{3, 3}
tx1:begin()
tx2:begin()
tx1("s:delete{2}")
tx1("s:select{}")
tx2("s:select{}")
tx2("s:delete{2}")
tx1("s:select{}")
tx2("s:select{}")
tx1:rollback()
tx2:commit()
s:select{}
s:truncate()

-- Stack of replacements
tx1:begin()
tx2:begin()
tx3:begin()
tx1("s:replace{1, 1}")
tx1("s:select{}")
s:select{}
tx2("s:replace{1, 2}")
tx1("s:select{}")
s:select{}
tx3("s:replace{1, 3}")
s:select{}
tx1("s:select{}")
tx2("s:select{}")
tx3("s:select{}")
tx1:commit()
s:select{}
tx2:commit()
s:select{}
tx3:commit()
s:select{}

s:drop()
s2:drop()

-- https://github.com/tarantool/tarantool/issues/5423
s = box.schema.space.create('test')
i1 = s:create_index('pk', {parts={{1, 'uint'}}})
i2 = s:create_index('sec', {parts={{2, 'uint'}}})

s:replace{1, 0}
s:delete{1}
collectgarbage()
s:replace{1, 1}
s:replace{1, 2 }

s:drop()

-- https://github.com/tarantool/tarantool/issues/5628
s = box.schema.space.create('test')
i = s:create_index('pk', {parts={{1, 'uint'}}})

s:replace{1, 0}
s:delete{1}
tx1:begin()
tx1("s:replace{1, 1}")
s:select{}
tx1:commit()
s:select{}

s:drop()

s = box.schema.space.create('test')
i = s:create_index('pk', {parts={{1, 'uint'}}})
s:replace{1, 0}

tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:select{1}')
tx2('s:replace{1, 2}')
tx1:commit()
tx2:commit()
s:select{}

s:drop()

s = box.schema.space.create('test')
i = s:create_index('pk')
s:replace{1}
collectgarbage('collect')
s:drop()

-- A bit of alter
s = box.schema.space.create('test')
i = s:create_index('pk')
s:replace{1, 1}
i = s:create_index('s', {parts={{2, 'unsigned'}}})
s:replace{1, 1, 2 }
s:select{}
s:drop()

-- Point holes
-- HASH
-- One select
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='hash'})
tx1:begin()
tx2:begin()
tx2('s:select{1}')
tx2('s:replace{2, 2, 2}')
tx1('s:replace{1, 1, 1}')
tx1:commit()
tx2:commit()
s:select{}
s:drop()

-- One hash get
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='hash'})
tx1:begin()
tx2:begin()
tx2('s:get{1}')
tx2('s:replace{2, 2, 2}')
tx1('s:replace{1, 1, 1}')
tx1:commit()
tx2:commit()
s:select{}
s:drop()

-- Same value get and select
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='hash'})
i2 = s:create_index('sk', {type='hash'})
tx1:begin()
tx2:begin()
tx3:begin()
tx2('s:select{1}')
tx2('s:replace{2, 2, 2}')
tx3('s:get{1}')
tx3('s:replace{3, 3, 3}')
tx1('s:replace{1, 1, 1}')
tx1:commit()
tx2:commit()
tx3:commit()
s:select{}
s:drop()

-- Different value get and select
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='hash'})
i2 = s:create_index('sk', {type='hash'})
tx1:begin()
tx2:begin()
tx3:begin()
tx1('s:select{1}')
tx2('s:get{2}')
tx1('s:replace{3, 3, 3}')
tx2('s:replace{4, 4, 4}')
tx3('s:replace{1, 1, 1}')
tx3('s:replace{2, 2, 2}')
tx3:commit()
tx1:commit()
tx2:commit()
s:select{}
s:drop()

-- Different value get and select but in coorrect orders
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='hash'})
i2 = s:create_index('sk', {type='hash'})
tx1:begin()
tx2:begin()
tx3:begin()
tx1('s:select{1}')
tx2('s:get{2}')
tx1('s:replace{3, 3, 3}')
tx2('s:replace{4, 4, 4}')
tx3('s:replace{1, 1, 1}')
tx3('s:replace{2, 2, 2}')
tx1:commit()
tx2:commit()
tx3:commit()
s:select{}
s:drop()

-- Repeatable read for hash index (issue gh-6040)
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='hash'})
i2 = s:create_index('sk', {type='hash'})
tx1:begin()
tx1('s:select{}')
s:replace{1, 1}
tx1('s:select{}')
tx1:commit()
s:select{}
s:drop()

--TREE
-- One select
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='tree'})
tx1:begin()
tx2:begin()
tx2('s:select{1}')
tx2('s:replace{2, 2, 2}')
tx1('s:replace{1, 1, 1}')
tx1:commit()
tx2:commit()
s:select{}
s:drop()

-- One get
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='tree'})
tx1:begin()
tx2:begin()
tx2('s:get{1}')
tx2('s:replace{2, 2, 2}')
tx1('s:replace{1, 1, 1}')
tx1:commit()
tx2:commit()
s:select{}
s:drop()

-- Same value get and select
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='tree'})
i2 = s:create_index('sk', {type='tree'})
tx1:begin()
tx2:begin()
tx3:begin()
tx2('s:select{1}')
tx2('s:replace{2, 2, 2}')
tx3('s:get{1}')
tx3('s:replace{3, 3, 3}')
tx1('s:replace{1, 1, 1}')
tx1:commit()
tx2:commit()
tx3:commit()
s:select{}
s:drop()

-- Different value get and select
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='tree'})
i2 = s:create_index('sk', {type='tree'})
tx1:begin()
tx2:begin()
tx3:begin()
tx1('s:select{1}')
tx2('s:get{2}')
tx1('s:replace{3, 3, 3}')
tx2('s:replace{4, 4, 4}')
tx3('s:replace{1, 1, 1}')
tx3('s:replace{2, 2, 2}')
tx3:commit()
tx1:commit()
tx2:commit()
s:select{}
s:drop()

-- Different value get and select but in coorrect orders
s = box.schema.space.create('test')
i1 = s:create_index('pk', {type='tree'})
i2 = s:create_index('sk', {type='tree'})
tx1:begin()
tx2:begin()
tx3:begin()
tx1('s:select{1}')
tx2('s:get{2}')
tx1('s:replace{3, 3, 3}')
tx2('s:replace{4, 4, 4}')
tx3('s:replace{1, 1, 1}')
tx3('s:replace{2, 2, 2}')
tx1:commit()
tx2:commit()
tx3:commit()
s:select{}
s:drop()

-- https://github.com/tarantool/tarantool/issues/5972
-- space:count and index:count
s = box.schema.create_space('test')
i1 = s:create_index('pk')

tx1:begin()
tx1('s:replace{1, 1, 1}')
tx1('s:count()')
s:count()
tx1:commit()
s:count()

tx1:begin()
tx1('s:delete{1}')
tx1('s:count()')
s:count()
tx1:commit()
s:count()

s:replace{1, 0}
s:replace{2, 0}
tx1:begin()
tx1('s:delete{2}')
tx1('s:count()')
tx1('s:replace{3, 1}')
tx1('s:count()')
tx1('s:replace{4, 1}')
tx1('s:count()')
tx2:begin()
tx2('s:replace{4, 2}')
tx2('s:count()')
tx2('s:replace{5, 2}')
tx2('s:count()')
tx2('s:delete{3}')
tx1('s:count()')
tx2('s:count()')
s:count()
tx1:commit()
tx2:commit()

s:truncate()

i2 = s:create_index('sk', {type = 'hash', parts={2,'unsigned'}})

_ = test_run:cmd("setopt delimiter ';'")
legend = 'status        i1:count() i2:count() tx1:i1:count() tx1:i2:count() tx2:i1:count() tx2:i2:count()'
function check()
    local res = ''
    local ok = true
    res = res .. '        ' .. tostring(i1:count())
    if (i1:count() ~= #i1:select{}) or (i1:len() ~= #i1:select{}) then
        ok = false
        res = res .. '!'
    end
    res = res .. '        ' .. tostring(i2:count())
    if (i2:count() ~= #i2:select{}) or (i1:len() ~= #i1:select{}) then
        ok = false
        res = res .. '!'
    end
    res = res .. '        ' .. tostring(tx1('i1:count()')[1])
    if (tx1('i1:count()')[1] ~= tx1('#i1:select{}')[1]) or (tx1('i1:len()')[1] ~= tx1('#i1:select{}')[1]) then
        ok = false
        res = res .. '!'
    end
    res = res .. '        ' .. tostring(tx1('i2:count()')[1])
    if (tx1('i2:count()')[1] ~= tx1('#i2:select{}')[1]) or (tx1('i2:len()')[1] ~= tx1('#i2:select{}')[1]) then
        ok = false
        res = res .. '!'
    end
    res = res .. '        ' .. tostring(tx2('i1:count()')[1])
    if (tx2('i1:count()')[1] ~= tx2('#i1:select{}')[1]) or (tx2('i1:len()')[1] ~= tx2('#i1:select{}')[1]) then
        ok = false
        res = res .. '!'
    end
    res = res .. '        ' .. tostring(tx2('i2:count()')[1])
    if (tx2('i2:count()')[1] ~= tx2('#i2:select{}')[1]) or (tx2('i2:len()')[1] ~= tx2('#i2:select{}')[1]) then
        ok = false
        res = res .. '!'
    end

    if ok then
        res = 'ok' .. res
    else
        res = 'fail' .. res
    end
    return res
end
_ = test_run:cmd("setopt delimiter ''");
legend

s:replace{1, 2}
s:replace{3, 4}
tx1:begin()
tx2:begin()
check()
tx1('s:replace{5, 42}')
check()
tx2('s:replace{6, 42}')
check()
tx1('s:delete{1}')
tx2('s:delete{3}')
check()
tx1('s:replace{8, 2}')
tx2('s:replace{3, 8}')
check()
tx1:commit()
tx2:commit()

-- Check that rolled back story is accounted correctly.
s:truncate()
tx1:begin()
tx1('s:replace{0, 0}')
tx1:rollback()
s:count()

-- Check that invisible read-view story is accounted correctly.
s:truncate()
s:insert{0, 0}
tx1:begin()
tx1('s:select{0}')
s:replace{0, 1}
tx1:commit()
s:delete{0}
s:count()

-- Check different orders
s:truncate()
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
tx1:commit()
tx2:commit()

s:truncate()
tx1:begin()
tx2:begin()
tx2('s:select{1}')
tx1('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
tx1:commit()
tx2:commit()

s:truncate()
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{1}')
tx2('s:replace{1, 2}')
tx1('s:replace{1, 1}')
tx1:commit()
tx2:commit()

s:truncate()
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2('s:select{1}')
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
tx2:commit()
tx1:commit()

-- https://github.com/tarantool/tarantool/issues/6131
s:truncate()
s:replace{1, 1}
tx1:begin()
tx2:begin()
tx1('s:select{1}')
tx2("s:update({1}, {{'=', 2, 2}})")
tx2:commit()
tx1("s:update({1}, {{'=', 2, 3}})")
tx1:commit()

s:drop()

-- https://github.com/tarantool/tarantool/issues/6140
s3 = box.schema.space.create('test3')
i31 = s3:create_index('pk', {parts={{1, 'uint'}}})
tx1:begin()
tx1('s3:replace{2}')
tx1('s3:select{}')
s3:drop()
tx1:rollback()

-- gh-6095: SQL query may crash in MVCC mode if it involves ephemeral spaces.
--
box.execute([[ CREATE TABLE test (id INT NOT NULL PRIMARY KEY, count INT NOT NULL)]])
box.execute([[ UPDATE test SET count = count + 1 WHERE id = 0 ]])
box.execute([[ DROP TABLE test]])

-- https://github.com/tarantool/tarantool/issues/5515
NUM_TEST_SPACES = 4
sp = {}
for k=1,NUM_TEST_SPACES do				\
	sp[k] = box.schema.space.create('test' .. k)	\
	sp[k]:create_index('test1', {parts={1}})	\
	sp[k]:create_index('test2', {parts={2}})	\
end
for k = 1,NUM_TEST_SPACES do				\
	for i = 1,100 do				\
		collectgarbage('collect')		\
		box.begin()				\
		sp[i % k + 1]:replace{1, 1, "hi", i}	\
		sp[i % k + 1]:delete{1}			\
		box.commit()				\
	end						\
end
for k=1,NUM_TEST_SPACES do				\
	sp[k]:drop()					\
end

-- Test all index types
s = box.schema.space.create('test')
i0 = s:create_index('pk', {parts={{1, 'uint'}}})
i1 = s:create_index('i1', {id = 10, type = 'tree', parts={{2, 'uint'}}})
i2 = s:create_index('i2', {id = 20, type = 'hash', parts={{2, 'uint'}}})
i3 = s:create_index('i3', {id = 30, type = 'bitset', parts={{3, 'uint'}}})
i4 = s:create_index('i4', {id = 40, type = 'rtree', parts={{4, 'array'}}})
s:replace{1, 1, 15, {0, 0}}
s:replace{1, 1, 7, {1, 1}}
s:replace{1, 2, 3, {2, 2}}
tx1:begin()
tx1('i1:select{2}')
tx1('i1:select{3}')
tx1('i1:count()')
tx1('i2:select{2}')
tx1('i2:select{3}')
tx1('i2:count()')
tx1('i3:select{3}')
tx1('i3:select{16}')
tx1('i3:count()')
tx1('i4:select{2, 2}')
tx1('i4:select{3, 3}')
tx1('i4:count()')
tx1:commit()
s:drop()

-- CASE 2 from #5515
fiber = require('fiber')
channel1 = fiber.channel()
channel2 = fiber.channel()
function test1()                                 \
    box.begin()                                  \
    local s = box.schema.space.create('test')    \
    s:create_index('pk')                         \
    channel1:put(1)                              \
    local ok = channel2:get()                    \
    box.session.push(ok)                         \
    box.rollback()                               \
    channel1:put(1)                              \
end
f = fiber.new(test1)
channel1:get()

assert(box.space.test == nil)
channel2:put(1)
channel1:get()
assert(box.space.test == nil)

-- https://github.com/tarantool/tarantool/issues/6137
_ = box.schema.create_space('t')
tx1:begin()
tx1("_ = box.space.t:create_index('i')")
tx1:commit()
box.space.t:drop()

-- https://github.com/tarantool/tarantool/issues/5892
box.execute([[CREATE TABLE u (column1 INT PRIMARY KEY, column2 INT);]])
box.schema.user.grant('guest', 'read,write', 'space', 'U')
conn = require('net.box').connect(box.cfg.listen)

box.execute([[INSERT INTO u VALUES (1, 20);]])
box.execute([[START TRANSACTION;]])
box.execute([[SELECT * FROM SEQSCAN u;]])

conn:execute([[UPDATE u SET column2 = 21;]])

box.execute([[UPDATE u SET column2 = 22;]])
box.execute([[COMMIT;]])
box.execute([[SELECT * FROM SEQSCAN u;]])

box.execute([[DROP TABLE u ;]])

--https://github.com/tarantool/tarantool/issues/6193
s = box.schema.create_space('test')
_ = s:create_index('pk')
fiber = require('fiber')
_ = fiber.create(function() box.space.test:replace({1, 2, 3}) end)
box.space.test:delete({1})
box.space.test:delete({1})
N = 1e4
fibers = {}
for i = 1, N do                            \
    local fib = fiber.new(function()       \
        box.begin()                        \
        box.space.test:replace({1, 2, 3})  \
    end)                                   \
    fib:set_joinable(true)                 \
    table.insert(fibers, fib)              \
end
for _,fib in pairs(fibers) do fib:join() end

s:drop()

-- Same key in primary, different in secondary.
s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})
s:replace{1, 1}
tx1:begin()
tx1('s:replace{1, 2}')
tx2:begin()
tx2('s:replace{1, 3}')
tx1('sk:select{1}')
tx2('sk:select{1}')
tx1:rollback();
tx2:rollback();
s:drop()

s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})
tx1:begin()
tx1('s:replace{1, 1}')
tx2:begin()
tx2('s:replace{1, 2}')
tx3:begin()
tx3('s:replace{1, 3}')
tx1:commit()
tx2('sk:select{}')
tx3('sk:select{}')
tx2:commit()
tx3:commit()
s:drop()

s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})
tx1:begin()
tx1('s:replace{1, 1}')
tx2:begin()
tx2('s:replace{1, 2}')
tx3:begin()
tx3('s:replace{1, 3}')
tx2:commit()
tx1('sk:select{}')
tx3('sk:select{}')
tx1:commit()
tx3:commit()
s:drop()

s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})
tx1:begin()
tx1('s:replace{1, 1}')
tx2:begin()
tx2('s:replace{1, 2}')
tx3:begin()
tx3('s:replace{1, 3}')
tx3:commit()
tx1('sk:select{}')
tx2('sk:select{}')
tx1:commit()
tx2:commit()
s:drop()

-- More complex conflict in secondary index.
s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})
tx1:begin()
tx2:begin()
tx1('s:replace{1, 1, 1}')
tx1('s:delete{1}')
tx1('s:replace{1, 1, 2}')
tx2('s:replace{2, 1, 1}')
tx2('s:delete{2}')
tx2('s:replace{2, 1, 2}')
tx1:commit()
tx2:commit()
s:drop()

s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})
tx1:begin()
tx2:begin()
tx1('s:replace{1, 1, 1}')
tx1('s:delete{1}')
tx1('s:replace{1, 1, 2}')
tx2('s:replace{2, 1, 1}')
tx2('s:delete{2}')
tx2('s:replace{2, 1, 2}')
tx2:commit() -- note that tx2 commits first.
tx1:commit()
s:drop()

-- Double deletes
s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
tx1:begin()
tx1('s:replace{1, 1}')
tx1('s:delete{1}')
tx1('s:delete{1}')
tx2:begin()
tx2('s:replace{1, 2}')
tx2('s:delete{1}')
tx2('s:delete{1}')
tx1:commit()
tx2:commit()
s:select{}
s:drop()

s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
tx1:begin()
tx1('s:replace{1, 1}')
tx1('s:delete{1}')
tx1('s:delete{1}')
tx2:begin()
tx2('s:replace{1, 2}')
tx2('s:delete{1}')
tx2('s:delete{1}')
tx2:commit()
tx1:commit()
s:select{}
s:drop()

--https://github.com/tarantool/tarantool/issues/6132
test_run:cmd("setopt delimiter ';'")
run_background_mvcc = true
function background_mvcc()
    while run_background_mvcc do
        box.space.accounts:update('petya', {{'+', 'balance', math.ceil(math.random() * 200) - 100}})
    end
end
test_run:cmd("setopt delimiter ''");

_ = box.schema.space.create('accounts', { format = {'name', 'balance'} })
_ = box.space.accounts:create_index('pk', { parts = { 1, 'string' } })
box.space.accounts:insert{ 'vasya', 0 }
box.space.accounts:insert{ 'petya', 0 }

fiber = require 'fiber'

tx1:begin()
tx1("box.space.accounts:update('vasya', {{'=', 'balance', 10}})")

tx2:begin()
tx2("box.space.accounts:update('vasya', {{'=', 'balance', 20}})")
tx2:commit()

fib = fiber.create(background_mvcc)
fib:set_joinable(true)
fiber.sleep(0.1)
run_background_mvcc = false
fib:join();

tx1:commit()
box.space.accounts:select{'vasya'}
box.space.accounts:drop()

--https://github.com/tarantool/tarantool/issues/6021
s = box.schema.create_space('test')
_ = s:create_index('pk', {parts={{1, 'uint'}}})

txn_proxy = require('txn_proxy')
tx1 = txn_proxy.new()
tx2 = txn_proxy.new()

s:insert({1, 0})

tx2:begin()
tx1:begin()

tx1("s:delete(1)")
tx2("s:replace({1, 3})")

tx2:commit()
tx1:commit()

s:drop()

s = box.schema.create_space('test')
_ = s:create_index('pk')
tx1:begin()
tx1('s:select{}')
tx2:begin()
tx2('s:replace{2, 2, 2}')
tx3:begin()
tx3('s:replace{1, 1, 1}')
tx3:commit()
tx1('s:select{}')
tx1:commit();
tx2:rollback()
s:drop()

--https://github.com/tarantool/tarantool/issues/6247
s = box.schema.create_space('test')
pk = s:create_index('pk', {parts={1, 'uint'}})
sk = s:create_index('sk', {parts={2, 'uint'}})

-- Make lots of changes to ensure that GC make the first tuple {1, 1,1} clean.
collectgarbage('collect')
for i = 1,10 do s:replace{i, i, i} end
collectgarbage('collect')
for i = 11,100 do s:replace{i, i, i} end
-- Make a new tuple that is definitely dirty now.
s:replace{0, 0, 0}
-- Hit dirty {0, 0, 0} in pk and conflict clean {1, 1, 1}.
s:replace{0, 1, 0}

s:drop()

--https://github.com/tarantool/tarantool/issues/6206
spc = box.schema.space.create('test')
_ = spc:create_index('test', {type='tree'})

tx1:begin()
tx1('spc:select{}')
spc:replace{1, 1}
tx1('spc:select{}')
tx1:commit()

spc:delete{1}
tx2 = txn_proxy.new()
tx2:begin()
tx2('spc:select{}')
spc:replace{1, 1}
tx2('spc:select{}')
tx2:commit()

spc:drop()

-- Check possible excess conflict: reference test
s = box.schema.create_space('test')
primary = s:create_index('pk', {parts={1, 'uint'}})
secondary = s:create_index('sk', {parts={2, 'uint'}})
tx1:begin()
tx1('secondary:select{1}')
s:replace{1, 2}
tx1('primary:select{1}')
tx1('s:replace{3, 3}')
tx1:commit()
s:drop()

-- Check possible excess conflict: test with deleted story
s = box.schema.create_space('test')
primary = s:create_index('pk', {parts={1, 'uint'}})
secondary = s:create_index('sk', {parts={2, 'uint'}})
s:replace{1, 1} -- The difference from the test above is that the tuple
s:delete{1}     -- is inserted and immediately deleted.
tx1:begin()
tx1('secondary:select{1}')
s:replace{1, 2}
tx1('primary:select{1}')
tx1('s:replace{3, 3}')
tx1:commit()
s:drop()

-- https://github.com/tarantool/tarantool/issues/6274
-- The code below must not crash.
for j = 1,100 do                                                     \
    s = box.schema.create_space('test')                              \
    primary = s:create_index('pk', {parts={1, 'uint'}})              \
    secondary = s:create_index('sk', {parts={2, 'uint'}})            \
    for i = 1,100 do s:replace{i,i} s:delete{i} end                  \
    s:drop()                                                         \
end

-- https://github.com/tarantool/tarantool/issues/6234
s = box.schema.create_space('test')
primary = s:create_index('pk', {parts={1, 'uint'}})
secondary = s:create_index('sk', {parts={2, 'uint'}})

-- Make a big transaction to make lots of garbage at once
tx1:begin()
for i = 1,10 do tx1('s:replace{i+100,i+100}') tx1('s:delete{i+100}') end
tx1:commit()

tx1:begin()
tx1("s:replace{1, 2, 'magic'}")
s:replace{1, 1, 'magic'} -- now {1, 2 ,'magic'} overwrites {1, 1, 'magic'}
tx1('s:delete{1}')
tx1:commit() -- succeeds

-- Make lost of changes to cause GC to run
for i = 11,50 do s:replace{i+100,i+100} s:delete{i+100} end

secondary:select{1} -- must be empty since we deleted {1}
s:replace{3, 1, 'magic'} -- must be OK.
s:drop()

-- Found by https://github.com/tarantool/tarantool/issues/5999
s=box.schema.space.create("s", {engine="memtx"})
ti=s:create_index("ti", {type="tree"})

tx1 = txn_proxy.new()
tx2 = txn_proxy.new()

tx1:begin() -- MUST BE OK
tx1("s:replace{6,'c'}") -- RES [[6,"c"]]
tx1("s:select{}") -- RES [[[6,"c"]]]
tx2:begin() -- MUST BE OK
tx2("s:replace{10,'cb'}") -- RES [[10,"cb"]]
s:insert{10, 'aaa'}
tx1:commit() -- MUST FAIL
tx2:commit() -- MUST BE OK

s:drop()

-- A pair of test that was created during #5999 investigation.
s=box.schema.space.create("s", {engine="memtx"})
ti=s:create_index("ti", {type="tree"})
tx1:begin()
tx2:begin()
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
rc1 = tx1('s:select{1}')
rc2 = tx2('s:select{1}')
tx1:commit() -- Must not fail
tx2:commit() -- Must not fail
s:drop()

s=box.schema.space.create("s", {engine="memtx"})
ti=s:create_index("ti", {type="tree"})
tx1:begin()
tx2:begin()
tx1('s:replace{1, 1}')
tx2('s:replace{1, 2}')
rc1 = tx1('s:select{1}')
rc2 = tx2('s:select{1}')
tx2:commit() -- Must not fail
tx1:commit() -- Must not fail
s:drop()

-- Found by https://github.com/tarantool/tarantool/issues/5999
s=box.schema.space.create("s", {engine="memtx"})
ti=s:create_index("ti", {type="tree"})
hi=s:create_index("hi", {parts={2}, type="hash"})

s:replace{1, 1, "original"}
tx2:begin()
tx2('s:replace{2, 1, "replace"}') -- fails like ordinal replace
tx1:begin()
tx1('s:insert{1, 2, "inserted"}') -- fails like ordinal insert
s:delete{1}
tx1('s:replace{10, 10}') -- make an op to become RW
tx1:commit() -- must fail since it actually saw {1, 1, "original"}
tx2('s:replace{11, 11}') -- make an op to become RW
tx2:commit() -- must fail since it actually saw {1, 1, "original"}
s:drop()

-- Found by https://github.com/tarantool/tarantool/issues/5999
-- https://github.com/tarantool/tarantool/issues/6325
s=box.schema.space.create("s", {engine="memtx"})
ti=s:create_index("ti", {type="tree"})
hi=s:create_index("hi", {type="hash"})

tx1:begin()
tx2:begin()
tx3:begin()

tx1("s:insert{1,'A'}")
tx2("s:insert{1,'B'}")
tx3("s:select{1}")
tx3("s:insert{2,'C'}")

tx1:rollback()
tx2:commit()
tx3:commit() -- Must fail as a RW reader of the rollbacked tx1
s:drop()

-- https://github.com/tarantool/tarantool/issues/5801
-- flaw #1
box.execute([[CREATE TABLE k1 (s1 INT PRIMARY KEY);]])
box.execute([[CREATE TABLE k2 (s1 INT PRIMARY KEY, s2 INT REFERENCES k1);]])
box.execute([[CREATE INDEX i1 ON k2(s2);]])
box.execute([[CREATE TABLE k3 (c INTEGER PRIMARY KEY AUTOINCREMENT);]])
box.execute([[CREATE TABLE k4 (s1 INT PRIMARY KEY);]])
box.schema.user.grant('guest', 'read,write', 'space', 'K1', {if_not_exists=true})
box.schema.user.grant('guest', 'read,write', 'space', 'K2', {if_not_exists=true})

net_box = require('net.box')
conn = net_box.connect(box.cfg.listen)

box.execute([[INSERT INTO k1 VALUES (1);]])
box.execute([[START TRANSACTION;]])
box.execute([[INSERT INTO k2 VALUES (99,1);]])

conn:execute([[DELETE FROM K1;]])
box.execute([[COMMIT;]])

-- flaw #2
box.execute([[DELETE FROM k2;]])
box.execute([[DELETE FROM k1;]])
tx1:begin()
tx1('box.execute([[SELECT COUNT() FROM SEQSCAN k1]])')
box.execute([[INSERT INTO k1 VALUES (1);]])
tx1('box.execute([[SELECT COUNT() FROM SEQSCAN k1]])')
tx1:commit()

box.execute([[DROP TABLE k4;]])
box.execute([[DROP TABLE k3;]])
box.execute([[DROP TABLE k2;]])
box.execute([[DROP TABLE k1;]])

-- gh-6318: make sure that space alter does not result in dirty read.
--
s3 = box.schema.space.create('test', { engine = 'memtx' })
_ = s3:create_index('primary')

format = {{name = 'field1', type = 'unsigned'}}
tx1:begin()
tx1('s3:replace{2}')
s3:select()
s3:alter({format = format})
s3:select{}
-- Alter operation aborts transaction, so results of tx1 should be rolled back.
--
tx1:commit()
s3:select()
s3:drop()

--gh-6263: basically the same as previous version but involves index creation.
--
s = box.schema.space.create('test')
_ = s:create_index('pk')

ch1 = fiber.channel()
ch2 = fiber.channel()

_ = test_run:cmd("setopt delimiter ';'")
fiber.create(function()
    box.begin()
    s:insert{1, 1}
    ch1:get()
    _ = pcall(box.commit)
    ch2:put(true)
end)
_ = test_run:cmd("setopt delimiter ''");

s:create_index('sk', {parts = {{2, 'unsigned'}}})

ch1:put(true)
ch2:get()

s.index.pk:select()
s.index.sk:select()

s:drop()

-- https://github.com/tarantool/tarantool/issues/6396

tx1:begin()
tx1("tx1_id = box.txn_id()")
tx2:begin()
tx2("tx2_id = box.txn_id()")
assert(tx1_id ~= tx2_id)
tx2:commit()
tx1("assert(tx1_id  == box.txn_id())")
tx1:commit()

test_run:cmd("switch default")
test_run:cmd("stop server tx_man")
test_run:cmd("cleanup server tx_man")
