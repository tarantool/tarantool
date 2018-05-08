env = require('test_run')
test_run = env.new()

fiber = require'fiber'

-- simple test for parallel ddl execution
_ = box.schema.space.create('test'):create_index('pk')

ch = fiber.channel(2)

test_run:cmd("setopt delimiter ';'")

function f1()
    box.space.test:create_index('sec', {parts = {2, 'num'}})
    ch:put(true)
end;

function f2()
    box.space.test:create_index('third', {parts = {3, 'string'}})
    ch:put(true)
end;

test_run:cmd("setopt delimiter ''");

_ = {fiber.create(f1), fiber.create(f2)}

ch:get()
ch:get()

_ = box.space.test:drop()

test_run:cmd('restart server default')

env = require('test_run')
test_run = env.new()
fiber = require'fiber'

ch = fiber.channel(2)

--issue #928
space = box.schema.space.create('test_trunc')
_ = space:create_index('pk')
_ = box.space.test_trunc:create_index('i1', {type = 'hash', parts = {2, 'STR'}})
_ = box.space.test_trunc:create_index('i2', {type = 'hash', parts = {2, 'STR'}})

function test_trunc() space:truncate() ch:put(true) end

_ = {fiber.create(test_trunc), fiber.create(test_trunc)}
_ = {ch:get(), ch:get()}
space:drop()

-- index should not crash after alter
space = box.schema.space.create('test_swap')
index = space:create_index('pk')
space:replace({1, 2, 3})
index:rename('primary')
index2 = space:create_index('sec')
space:replace({2, 3, 1})
space:select()
space:drop()


ch = fiber.channel(3)

_ = box.schema.space.create('test'):create_index('pk')

test_run:cmd("setopt delimiter ';'")
function add_index()
    box.space.test:create_index('sec', {parts = {2, 'num'}})
    ch:put(true)
end;

function insert_tuple(tuple)
    ch:put({pcall(box.space.test.replace, box.space.test, tuple)})
end;
test_run:cmd("setopt delimiter ''");

_ = {fiber.create(insert_tuple, {1, 2, 'a'}), fiber.create(add_index), fiber.create(insert_tuple, {2, '3', 'b'})}
{ch:get(), ch:get(), ch:get()}

box.space.test:select()

test_run:cmd('restart server default')

box.space.test:select()
box.space.test:drop()

-- gh-2336 crash if format called twice during snapshot
fiber = require'fiber'

space = box.schema.space.create('test_format')
_ = space:create_index('pk', { parts = { 1,'str' }})
space:format({{ name ="key"; type = "string" }, { name ="dataAB"; type = "string" }})
str = string.rep("t",1024)
for i = 1, 10000 do space:insert{tostring(i), str} end
ch = fiber.channel(3)
_ = fiber.create(function() fiber.yield() box.snapshot() ch:put(true) end)
format = {{name ="key"; type = "string"}, {name ="data"; type = "string"}}
for i = 1, 2 do fiber.create(function() fiber.yield() space:format(format) ch:put(true) end) end

{ch:get(), ch:get(), ch:get()}

space:drop()

-- collation
function setmap(table) return setmetatable(table, { __serialize = 'map' }) end

box.internal.collation.create('test')
box.internal.collation.create('test', 'ICU')
box.internal.collation.create(42, 'ICU', 'ru_RU')
box.internal.collation.create('test', 42, 'ru_RU')
box.internal.collation.create('test', 'ICU', 42)
box.internal.collation.create('test', 'nothing', 'ru_RU')
box.internal.collation.create('test', 'ICU', 'ru_RU', setmap{}) --ok
box.internal.collation.create('test', 'ICU', 'ru_RU')
box.internal.collation.drop('test')
box.internal.collation.drop('nothing') -- allowed
box.internal.collation.create('test', 'ICU', 'ru_RU', 42)
box.internal.collation.create('test', 'ICU', 'ru_RU', 'options')
box.internal.collation.create('test', 'ICU', 'ru_RU', {ping='pong'})
box.internal.collation.create('test', 'ICU', 'ru_RU', {french_collation='german'})
box.internal.collation.create('test', 'ICU', 'ru_RU', {french_collation='on'}) --ok
box.internal.collation.drop('test') --ok
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength='supervillian'})
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength=42})
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength=2}) --ok
box.internal.collation.drop('test') --ok
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength='primary'}) --ok
box.internal.collation.drop('test') --ok

box.begin() box.internal.collation.create('test2', 'ICU', 'ru_RU')
box.rollback()

box.internal.collation.create('test', 'ICU', 'ru_RU')
box.internal.collation.exists('test')

test_run:cmd('restart server default')
function setmap(table) return setmetatable(table, { __serialize = 'map' }) end

box.internal.collation.exists('test')
box.internal.collation.drop('test')

box.space._collation:auto_increment{'test'}
box.space._collation:auto_increment{'test', 0, 'ICU'}
box.space._collation:auto_increment{'test', 'ADMIN', 'ICU', 'ru_RU'}
box.space._collation:auto_increment{42, 0, 'ICU', 'ru_RU'}
box.space._collation:auto_increment{'test', 0, 42, 'ru_RU'}
box.space._collation:auto_increment{'test', 0, 'ICU', 42}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', setmap{}} --ok
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', setmap{}}
box.space._collation.index.name:delete{'test'} -- ok
box.space._collation.index.name:delete{'nothing'} -- allowed
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', 42}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', 'options'}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', {ping='pong'}}
opts = {normalization_mode='NORMAL'}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.normalization_mode = 'OFF'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} -- ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.numeric_collation = 'PERL'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.numeric_collation = 'ON'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.alternate_handling1 = 'ON'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.alternate_handling1 = nil
opts.alternate_handling = 'ON'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.alternate_handling = 'SHIFTED'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.case_first = 'ON'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.case_first = 'OFF'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.case_level = 'UPPER'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.case_level = 'DEFAULT'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok

box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', setmap{}}
box.space._collation:select{}
test_run:cmd('restart server default')
box.space._collation:select{}
box.space._collation.index.name:delete{'test'}

--
-- gh-2839: allow to store custom fields in field definition.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {'field2', 'unsigned'}
format[3] = {'field3', 'unsigned', custom_field = 'custom_value'}
s = box.schema.create_space('test', {format = format})
s:format()[3].custom_field
s:drop()

--
-- gh-2937: allow to specify collation in field definition.
--
format = {}
format[1] = {name = 'field1', type = 'string', collation = 'unicode'}
format[2] = {'field2', 'any', collation = 'unicode_ci'}
format[3] = {type = 'scalar', name = 'field3', collation = 'unicode'}
s = box.schema.create_space('test', {format = format})
s:format()
s:drop()

-- Check that collation is allowed only for stings, scalar and any types.
format = {}
format[1] = {'field1', 'unsigned', collation = 'unicode'}
s = box.schema.create_space('test', {format = format})
format[1] = {'field2', 'array', collation = 'unicode_ci'}
s = box.schema.create_space('test', {format = format})

-- Check that error is raised when collation doesn't exists.
format = {}
format[1] = {'field1', 'unsigend', collation = 'test_coll'}
s = box.schema.create_space('test', {format = format})

-- Check that error is raised when collation with wrong id is used.
_space = box.space[box.schema.SPACE_ID]
utils = require('utils')
EMPTY_MAP = utils.setmap({})
format = {{name = 'field1', type = 'string', collation = 666}}
surrogate_space = {12345, 1, 'test', 'memtx', 0, EMPTY_MAP, format}
_space:insert(surrogate_space)

--
-- gh-2783
-- A ddl operation shoud fail before trying to lock a ddl latch
-- in a multi-statement transaction.
-- If operation tries to lock already an locked latch then the
-- current transaction will be silently rolled back under our feet.
-- This is confusing. So check for multi-statement transaction
-- before locking the latch.
--
test_latch = box.schema.space.create('test_latch')
_ = test_latch:create_index('primary', {unique = true, parts = {1, 'unsigned'}})
fiber = require('fiber')
c = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    test_latch:create_index("sec", {unique = true, parts = {2, 'unsigned'}})
    c:put(true)
end);

box.begin()
test_latch:create_index("sec2", {unique = true, parts = {2, 'unsigned'}})
box.commit();

test_run:cmd("setopt delimiter ''");

_ = c:get()
test_latch:drop() -- this is where everything stops
