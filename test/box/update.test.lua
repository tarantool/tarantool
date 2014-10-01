s = box.schema.create_space('tweedledum')
s:create_index('pk')

-- test delete field
s:insert{1000001, 1000002, 1000003, 1000004, 1000005}
s:update({1000001}, {{'#', 1, 1}})
s:update({1000002}, {{'#', 1, 1}})
s:update({1000003}, {{'#', 1, 1}})
s:update({1000004}, {{'#', 1, 1}})
s:update({1000005}, {{'#', 1, 1}})
s:truncate()

-- test arithmetic
s:insert{1, 0}
s:update(1, {{'+', 2, 10}})
s:update(1, {{'+', 2, 15}})
s:update(1, {{'-', 2, 5}})
s:update(1, {{'-', 2, 20}})
s:update(1, {{'|', 2, 0x9}})
s:update(1, {{'|', 2, 0x6}})
s:update(1, {{'&', 2, 0xabcde}})
s:update(1, {{'&', 2, 0x2}})
s:update(1, {{'^', 2, 0xa2}})
s:update(1, {{'^', 2, 0xa2}})
s:truncate()

-- test delete multiple fields
s:insert{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
s:update({0}, {{'#', 42, 1}})
s:update({0}, {{'#', 4, 'abirvalg'}})
s:update({0}, {{'#', 2, 1}, {'#', 4, 2}, {'#', 6, 1}})
s:update({0}, {{'#', 4, 3}})
s:update({0}, {{'#', 5, 123456}})
s:update({0}, {{'#', 3, 4294967295}})
s:update({0}, {{'#', 2, 0}})
s:truncate()

-- test insert field
s:insert{1, 3, 6, 9}
s:update({1}, {{'!', 2, 2}})
s:update({1}, {{'!', 4, 4}, {'!', 4, 5}, {'!', 5, 7}, {'!', 5, 8}})
s:update({1}, {{'!', 10, 10}, {'!', 10, 11}, {'!', 10, 12}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'#', 2, 1}, {'!', 2, 'inserted tuple'}, {'=', 3, 'set tuple'}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'=', 2, 'set tuple'}, {'!', 2, 'inserted tuple'}, {'#', 3, 1}})
s:update({1}, {{'!', 1, 3}, {'!', 1, 2}})
s:truncate()

-- test update's assign opearations
s:replace{1, 'field string value'}
s:update({1}, {{'=', 2, 'new field string value'}, {'=', 3, 42}, {'=', 4, 0xdeadbeef}})

-- test multiple update opearations on the same field
s:update({1}, {{'+', 3, 16}, {'&', 4, 0xffff0000}, {'|', 4, 0x0000a0a0}, {'^', 4, 0xffff00aa}})

-- test update splice operation
s:replace{1953719668, 'something to splice'}
s:update(1953719668, {{':', 2, 1, 4, 'no'}})
s:update(1953719668, {{':', 2, 1, 2, 'every'}})
-- check an incorrect offset
s:update(1953719668, {{':', 2, 100, 2, 'every'}})
s:update(1953719668, {{':', 2, -100, 2, 'every'}})
s:truncate()
s:insert{1953719668, 'hello', 'october', '20th'}:unpack()
s:truncate()
s:insert{1953719668, 'hello world'}
s:update(1953719668, {{'=', 2, 'bye, world'}})
s:delete{1953719668}

s:replace({10, 'abcde'})
s:update(10,  {{':', 2, 0, 0, '!'}})
s:update(10,  {{':', 2, 1, 0, '('}})
s:update(10,  {{':', 2, 2, 0, '({'}})
s:update(10,  {{':', 2, -1, 0, ')'}})
s:update(10,  {{':', 2, -2, 0, '})'}})

-- test update delete operations
s:update({1}, {{'#', 4, 1}, {'#', 3, 1}})

-- test update insert operations
s:update({1}, {{'!', 2, 1}, {'!', 2, 2}, {'!', 2, 3}, {'!', 2, 4}})

-- s:update: zero field
s:insert{48}
s:update(48, {{'=', 0, 'hello'}})

-- s:update: push/pop fields
s:insert{1684234849}
s:update({1684234849}, {{'#', 2, 1}})
s:update({1684234849}, {{'=', -1, 'push1'}})
s:update({1684234849}, {{'=', -1, 'push2'}})
s:update({1684234849}, {{'=', -1, 'push3'}})
s:update({1684234849}, {{'#', 2, 1}, {'=', -1, 'swap1'}})
s:update({1684234849}, {{'#', 2, 1}, {'=', -1, 'swap2'}})
s:update({1684234849}, {{'#', 2, 1}, {'=', -1, 'swap3'}})
s:update({1684234849}, {{'#', -1, 1}, {'=', -1, 'noop1'}})
s:update({1684234849}, {{'#', -1, 1}, {'=', -1, 'noop2'}})
s:update({1684234849}, {{'#', -1, 1}, {'=', -1, 'noop3'}})

--
-- #416: UPDATEs from Lua can't be properly restored due to one based indexing
--
--# stop server default
--# start server default

s = box.space.tweedledum
s:select{}
s:truncate()
s:drop()

-- #521: Cryptic error message in update operation
s = box.schema.create_space('tweedledum')
s:create_index('pk')
s:insert{1, 2, 3}
s:update({1})
s:update({1}, {'=', 1, 1})
s:drop()
