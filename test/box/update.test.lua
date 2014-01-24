s = box.schema.create_space('tweedledum')
s:create_index('pk')

-- test delete field
s:insert{1000001, 1000002, 1000003, 1000004, 1000005}
s:update({1000001}, {{'#', 0, 1}})
s:update({1000002}, {{'#', 0, 1}})
s:update({1000003}, {{'#', 0, 1}})
s:update({1000004}, {{'#', 0, 1}})
s:update({1000005}, {{'#', 0, 1}})
s:truncate()

-- test delete multiple fields
s:insert{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
s:update({0}, {{'#', 42, 1}})
s:update({0}, {{'#', 3, 'abirvalg'}})
s:update({0}, {{'#', 1, 1}, {'#', 3, 2}, {'#', 5, 1}})
s:update({0}, {{'#', 3, 3}})
s:update({0}, {{'#', 4, 123456}})
s:update({0}, {{'#', 2, 4294967295}})
s:update({0}, {{'#', 1, 0}})
s:truncate()

-- test insert field
s:insert{1, 3, 6, 9}
s:update({1}, {{'!', 1, 2}})
s:update({1}, {{'!', 3, 4}, {'!', 3, 5}, {'!', 4, 7}, {'!', 4, 8}})
s:update({1}, {{'!', 9, 10}, {'!', 9, 11}, {'!', 9, 12}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'#', 1, 1}, {'!', 1, 'inserted tuple'}, {'=', 2, 'set tuple'}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'=', 1, 'set tuple'}, {'!', 1, 'inserted tuple'}, {'#', 2, 1}})
s:update({1}, {{'!', 0, 3}, {'!', 0, 2}})
s:truncate()

-- test update's assign opearations
s:replace{1, 'field string value'}
s:update({1}, {{'=', 1, 'new field string value'}, {'=', 2, 42}, {'=', 3, 0xdeadbeef}})

-- test multiple update opearations on the same field
s:update({1}, {{'+', 2, 16}, {'&', 3, 0xffff0000}, {'|', 3, 0x0000a0a0}, {'^', 3, 0xffff00aa}})

-- test update splice operation
s:update({1}, {{':', 1, 0, 3, 'the newest'}})

s:replace{1953719668, 'something to splice'}
s:update(1953719668, {{':', 1, 0, 4, 'no'}})
s:update(1953719668, {{':', 1, 0, 2, 'every'}})
-- check an incorrect offset
s:update(1953719668, {{':', 1, 100, 2, 'every'}})
s:update(1953719668, {{':', 1, -100, 2, 'every'}})
s:truncate()
s:insert{1953719668, 'hello', 'october', '20th'}:unpack()
s:truncate()
s:insert{1953719668, 'hello world'}
s:update(1953719668, {{'=', 1, 'bye, world'}})
s:delete{1953719668}

-- test update delete operations
s:update({1}, {{'#', 3, 1}, {'#', 2, 1}})

-- test update insert operations
s:update({1}, {{'!', 1, 1}, {'!', 1, 2}, {'!', 1, 3}, {'!', 1, 4}})

s:truncate()

-- s:update: push/pop fields
s:insert{1684234849}
s:update({1684234849}, {{'#', 1, 1}})
s:update({1684234849}, {{'=', -1, 'push1'}})
s:update({1684234849}, {{'=', -1, 'push2'}})
s:update({1684234849}, {{'=', -1, 'push3'}})
s:update({1684234849}, {{'#', 1, 1}, {'=', -1, 'swap1'}})
s:update({1684234849}, {{'#', 1, 1}, {'=', -1, 'swap2'}})
s:update({1684234849}, {{'#', 1, 1}, {'=', -1, 'swap3'}})
s:update({1684234849}, {{'#', -1, 1}, {'=', -1, 'noop1'}})
s:update({1684234849}, {{'#', -1, 1}, {'=', -1, 'noop2'}})
s:update({1684234849}, {{'#', -1, 1}, {'=', -1, 'noop3'}})

s:drop()
