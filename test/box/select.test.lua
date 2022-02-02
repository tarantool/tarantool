msgpack = require('msgpack')
env = require('test_run')
test_run = env.new()

s = box.schema.space.create('select', { temporary = true })
index1 = s:create_index('primary', { type = 'tree' })
index2 = s:create_index('second', { type = 'tree', unique = true,  parts = {2, 'unsigned', 1, 'unsigned'}})
for i = 1, 20 do s:insert({ i, 1, 2, 3 }) end

test_run:cmd("setopt delimiter ';'")
local function test_op(op, idx, ...)
    local t1 = idx[op .. '_ffi'](idx, ...)
    local t2 = idx[op .. '_luac'](idx, ...)
    if msgpack.encode(t1) ~= msgpack.encode(t2) then
        return 'different result from '..op..'_ffi and '..op..'_luac', t1, t2
    end
    return t1
end
test = setmetatable({}, {
    __index = function(_, op) return function(...) return test_op(op, ...) end end
});
test_run:cmd("setopt delimiter ''");


--------------------------------------------------------------------------------
-- get tests
--------------------------------------------------------------------------------

s.index[0].get == s.index[0].get_ffi or s.index[0].get == s.index[0].get_luac

test.get(s.index[0])
test.get(s.index[0], {})
test.get(s.index[0], nil)
test.get(s.index[0], 1)
test.get(s.index[0], {1})
test.get(s.index[0], {1, 2})
test.get(s.index[0], 0)
test.get(s.index[0], {0})
test.get(s.index[0], "0")
test.get(s.index[0], {"0"})

test.get(s.index[1], 1)
test.get(s.index[1], {1})
test.get(s.index[1], {1, 2})

--------------------------------------------------------------------------------
-- select tests
--------------------------------------------------------------------------------

s.index[0].select == s.index[0].select_ffi or s.index[0].select == s.index[0].select_luac

test.select(s.index[0])
test.select(s.index[0], {})
test.select(s.index[0], nil)
test.select(s.index[0], {}, {iterator = 'ALL'})

test.select(s.index[0], nil, {iterator = box.index.ALL })
test.select(s.index[0], {}, {iterator = box.index.ALL, limit = 10})
test.select(s.index[0], nil, {iterator = box.index.ALL, limit = 0})
test.select(s.index[0], {}, {iterator = 'ALL', limit = 1, offset = 15})
test.select(s.index[0], nil, {iterator = 'ALL', limit = 20, offset = 15})

test.select(s.index[0], nil, {iterator = box.index.EQ})
test.select(s.index[0], {}, {iterator = 'EQ'})
test.select(s.index[0], nil, {iterator = 'REQ'})
test.select(s.index[0], {}, {iterator = box.index.REQ})

test.select(s.index[0], nil, {iterator = 'EQ', limit = 2, offset = 1})
test.select(s.index[0], {}, {iterator = box.index.REQ, limit = 2, offset = 1})

test.select(s.index[0], 1)
test.select(s.index[0], {1})
test.select(s.index[0], {1, 2})
test.select(s.index[0], 0)
test.select(s.index[0], {0})
test.select(s.index[0], "0")
test.select(s.index[0], {"0"})

test.select(s.index[1], 1)
test.select(s.index[1], {1})
test.select(s.index[1], {1}, {limit = 2})
test.select(s.index[1], 1, {iterator = 'EQ'})
test.select(s.index[1], {1}, {iterator = box.index.EQ, offset = 16, limit = 2})
test.select(s.index[1], {1}, {iterator = box.index.REQ, offset = 16, limit = 2 })
test.select(s.index[1], {1, 2}, {iterator = 'EQ'})
test.select(s.index[1], {1, 2}, {iterator = box.index.REQ})
test.select(s.index[1], {1, 2})

test.select(s.index[0], nil, { iterator = 'ALL', offset = 0, limit = 4294967295 })
test.select(s.index[0], {}, { iterator = 'ALL', offset = 0, limit = 4294967295 })

test.select(s.index[0], 1)
test.select(s.index[0], 1, { iterator = box.index.EQ })
test.select(s.index[0], 1, { iterator = 'EQ' })
test.select(s.index[0], 1, { iterator = 'GE' })
test.select(s.index[0], 1, { iterator = 'GE', limit = 2 })
test.select(s.index[0], 1, { iterator = 'LE', limit = 2 })
test.select(s.index[0], 1, { iterator = 'GE', offset = 10, limit = 2 })

s:select(2)

--------------------------------------------------------------------------------
-- min/max tests
--------------------------------------------------------------------------------

test.min(s.index[1])
test.max(s.index[1])

--------------------------------------------------------------------------------
-- count tests
--------------------------------------------------------------------------------

test.count(s.index[1])
test.count(s.index[0], nil)
test.count(s.index[0], {})
test.count(s.index[0], 10, { iterator = 'GT'})

--------------------------------------------------------------------------------
-- random tests
--------------------------------------------------------------------------------

test.random(s.index[0], 48)

s:drop()

collectgarbage('collect')

-- gh-3224 resurrect tuple bigrefs

collectgarbage('stop')
s = box.schema.space.create('select', { temporary = true })
index = s:create_index('primary', { type = 'tree' })
_ = s:insert{0}
_ = s:insert{1}
_ = s:insert{2}
_ = s:insert{3}
lots_of_links = setmetatable({}, {__mode = 'v'})
i = 0
while (i < 33000) do table.insert(lots_of_links, s:get{0}) i = i + 1 end
while (i < 66000) do table.insert(lots_of_links, s:get{1}) i = i + 1 end
while (i < 100000) do table.insert(lots_of_links, s:get{2}) i = i + 1 end
ref_count = 0
for k, v in pairs(lots_of_links) do ref_count = ref_count + 1 end
ref_count
-- check that tuples are deleted after gc is activated
collectgarbage('collect')
-- GC should be restarted after it was stopped.
collectgarbage('restart')
lots_of_links
s:drop()

-- gh-6501 attempt to index local 'opts' (a number value)

s = box.schema.space.create('select', { temporary = true })
_ = s:create_index('primary', { type = 'tree' })
_ = s:insert{0}
_ = s:insert{1}
_ = s:insert{2}
s:select(0, true)
s:select(0, box.NULL)
s:select(0, box.index.GT)
s:select(0, 'GT')
s:select(0, {iterator = box.index.GT})
s:select(0, {iterator = 'GT'})
_ = s:pairs(nil, true)
_ = s:pairs(nil, box.index.GT)
_ = s:pairs(nil, 'GT')
s:count(0, true)
s:count(0, box.index.GT)
s:count(0, 'GT')
s:drop()
