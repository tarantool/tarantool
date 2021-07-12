env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

txn_proxy = require('txn_proxy')

s = box.schema.space.create('test')
i1 = s:create_index('pk', {parts={{1, 'uint'}}})
i2 = s:create_index('sec', {parts={{2, 'uint'}}})

s2 = box.schema.space.create('test2')
i21 = s2:create_index('pk', {parts={{1, 'uint'}}})
i22 = s2:create_index('sec', {parts={{2, 'uint'}}})

tx1 = txn_proxy.new()
tx2 = txn_proxy.new()
tx3 = txn_proxy.new()

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
tx1('box.space.test:select()')
tx1('box.space.test:replace({1, 2, 3})')

assert(box.space.test ~= nil)
channel2:put(1)
channel1:get()
assert(box.space.test == nil)

-- "100% reproducer" from #5515
box.begin()
s = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
box.space.test:replace({1,2,3})
tx1:begin()
tx1('s:replace{2,2,3}');
tx1('s:replace{3,2,3}');
tx1:commit()
box.space.test:truncate()
assert(box.space.test ~= nil)
box.rollback()
assert(box.space.test == nil)
collectgarbage()
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
box.execute([[SELECT * FROM u;]])

conn:execute([[UPDATE u SET column2 = 21;]])

box.execute([[UPDATE u SET column2 = 22;]])
box.execute([[COMMIT;]])
box.execute([[SELECT * FROM u;]])

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

test_run:cmd("switch default")
test_run:cmd("stop server tx_man")
test_run:cmd("cleanup server tx_man")
