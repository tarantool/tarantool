msgpack = require('msgpack')

s = box.schema.space.create('select', { temporary = true })
index1 = s:create_index('primary', { type = 'tree' })
index2 = s:create_index('second', { type = 'tree', unique = true,  parts = {2, 'num', 1, 'num'}})
for i = 1, 20 do s:insert({ i, 1, 2, 3 }) end

--------------------------------------------------------------------------------
-- get tests
--------------------------------------------------------------------------------

s.index[0]:get()
s.index[0]:get({})
s.index[0]:get(nil)
s.index[0]:get(1)
s.index[0]:get({1})
s.index[0]:get({1, 2})
s.index[0]:get(0)
s.index[0]:get({0})
s.index[0]:get("0")
s.index[0]:get({"0"})

s.index[1]:get(1)
s.index[1]:get({1})
s.index[1]:get({1, 2})

--------------------------------------------------------------------------------
-- select tests
--------------------------------------------------------------------------------

--# setopt delimiter ';'
function test(idx, ...)
    local t1 = idx:select_ffi(...)
    local t2 = idx:select_luac(...)
    if msgpack.encode(t1) ~= msgpack.encode(t2) then
        return 'different result from select_ffi and select_luac', t1, t2
    end
    return t1
end;
--# setopt delimiter ''

s.index[0].select == s.index[0].select_ffi or s.index[0].select == s.index[0].select_luac

test(s.index[0])
test(s.index[0], {})
test(s.index[0], nil)
test(s.index[0], {}, {iterator = 'ALL'})

test(s.index[0], nil, {iterator = box.index.ALL })
test(s.index[0], {}, {iterator = box.index.ALL, limit = 10})
test(s.index[0], nil, {iterator = box.index.ALL, limit = 0})
test(s.index[0], {}, {iterator = 'ALL', limit = 1, offset = 15})
test(s.index[0], nil, {iterator = 'ALL', limit = 20, offset = 15})

test(s.index[0], nil, {iterator = box.index.EQ})
test(s.index[0], {}, {iterator = 'EQ'})
test(s.index[0], nil, {iterator = 'REQ'})
test(s.index[0], {}, {iterator = box.index.REQ})

test(s.index[0], nil, {iterator = 'EQ', limit = 2, offset = 1})
test(s.index[0], {}, {iterator = box.index.REQ, limit = 2, offset = 1})

test(s.index[0], 1)
test(s.index[0], {1})
test(s.index[0], {1, 2})
test(s.index[0], 0)
test(s.index[0], {0})
test(s.index[0], "0")
test(s.index[0], {"0"})

test(s.index[1], 1)
test(s.index[1], {1})
test(s.index[1], {1}, {limit = 2})
test(s.index[1], 1, {iterator = 'EQ'})
test(s.index[1], {1}, {iterator = box.index.EQ, offset = 16, limit = 2})
test(s.index[1], {1}, {iterator = box.index.REQ, offset = 16, limit = 2 })
test(s.index[1], {1, 2}, {iterator = 'EQ'})
test(s.index[1], {1, 2}, {iterator = box.index.REQ})
test(s.index[1], {1, 2})

test(s.index[0], nil, { iterator = 'ALL', offset = 0, limit = 4294967295 })
test(s.index[0], {}, { iterator = 'ALL', offset = 0, limit = 4294967295 })

test(s.index[0], 1)
test(s.index[0], 1, { iterator = box.index.EQ })
test(s.index[0], 1, { iterator = 'EQ' })
test(s.index[0], 1, { iterator = 'GE' })
test(s.index[0], 1, { iterator = 'GE', limit = 2 })
test(s.index[0], 1, { iterator = 'LE', limit = 2 })
test(s.index[0], 1, { iterator = 'GE', offset = 10, limit = 2 })

s:select(2)

s:drop()

s = box.schema.space.create('select', { temporary = true })
index = s:create_index('primary', { type = 'tree' })
local a s:insert{0}
lots_of_links = {}
ref_count = 0
while (true) do table.insert(lots_of_links, s:get{0}) ref_count = ref_count + 1 end
ref_count
lots_of_links = {}
s:drop()
