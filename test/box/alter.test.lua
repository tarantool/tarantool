_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
ADMIN = 1
env = require('test_run')
test_run = env.new()
test_run:cmd("push filter ', .lsn.: [0-9]+' to ''")
utils = require('utils')
EMPTY_MAP = utils.setmap({})

--
-- Test insertion into a system space - verify that
-- mandatory fields are required.
--
_space:insert{_space.id, ADMIN, 'test', 'memtx', 0, EMPTY_MAP, {}}
--
-- Bad space id
--
_space:insert{'hello', 'world', 'test', 'memtx', 0, EMPTY_MAP, {}}
--
-- Can't create a space which has wrong field count - field_count must be NUM
--
_space:insert{_space.id, ADMIN, 'test', 'world', 0, EMPTY_MAP, {}}
--
-- There is already a tuple for the system space
--
_space:insert{_space.id, ADMIN, '_space', 'memtx', 0, EMPTY_MAP, {}}
_space:replace{_space.id, ADMIN, '_space', 'memtx', 0, EMPTY_MAP, {}}
_space:insert{_index.id, ADMIN, '_index', 'memtx', 0, EMPTY_MAP, {}}
_space:replace{_index.id, ADMIN, '_index', 'memtx', 0, EMPTY_MAP, {}}
--
-- Can't change properties of a space
--
_space:replace{_space.id, ADMIN, '_space', 'memtx', 0, EMPTY_MAP, {}}
--
-- Can't drop a system space
--
_space:delete{_space.id}
_space:delete{_index.id}
--
-- Can't change properties of a space
--
_space:update({_space.id}, {{'-', 1, 1}})
_space:update({_space.id}, {{'-', 1, 2}})
--
-- Create a space
--
t = _space:auto_increment{ADMIN, 'hello', 'memtx', 0, EMPTY_MAP, {}}
-- Check that a space exists
space = box.space[t[1]]
space.id
space.field_count
space.index[0]
--
-- check dml - the space has no indexes yet, but must not crash on DML
--
space:select{0}
space:insert{0, 0}
space:replace{0, 0}
space:update({0}, {{'+', 1, 1}})
space:delete{0}
t = _space:delete{space.id}
space_deleted = box.space[t[1]]
space_deleted
space:replace{0}
_index:insert{_space.id, 0, 'primary', 'tree', {unique=true}, {{0, 'unsigned'}}}
_index:replace{_space.id, 0, 'primary', 'tree', {unique=true}, {{0, 'unsigned'}}}
_index:insert{_index.id, 0, 'primary', 'tree', {unique=true}, {{0, 'unsigned'}, {1, 'unsigned'}}}
_index:replace{_index.id, 0, 'primary', 'tree', {unique=true}, {{0, 'unsigned'}, {1, 'unsigned'}}}
-- access_sysview.test changes output of _index:select{}.
-- let's change _index space in such a way that it will be
-- uniformn weather access_sysview.test is completed of not.
box.space._space.index.owner:alter{parts = {2, 'unsigned'}}
box.space._vspace.index.owner:alter{parts = {2, 'unsigned'}}
_index:select{}
-- modify indexes of a system space
_index:delete{_index.id, 0}
_space:insert{1000, ADMIN, 'hello', 'memtx', 0, EMPTY_MAP, {}}
_index:insert{1000, 0, 'primary', 'tree', {unique=true}, {{0, 'unsigned'}}}
box.space[1000]:insert{0, 'hello, world'}
box.space[1000]:drop()
box.space[1000]
-- test that after disabling triggers on system spaces we still can
-- get a correct snapshot
_index:run_triggers(false)
_space:run_triggers(false)
box.snapshot()
test_run:cmd("restart server default with cleanup=1")
utils = require('utils')
EMPTY_MAP = utils.setmap({})
ADMIN = 1
box.space['_space']:insert{1000, ADMIN, 'test', 'memtx', 0, EMPTY_MAP, {}}
box.space[1000].id
box.space['_space']:delete{1000}
box.space[1000]

--------------------------------------------------------------------------------
-- #197: box.space.space0:len() returns an error if there is no index
--------------------------------------------------------------------------------

space = box.schema.space.create('gh197')
space:len()
space:truncate()
space:pairs():totable()
space:drop()

--------------------------------------------------------------------------------
-- #198: names like '' and 'x.y' and 5 and 'primary ' are legal
--------------------------------------------------------------------------------

-- invalid identifiers
box.schema.space.create('invalid.identifier')
box.schema.space.create('invalid identifier')
box.schema.space.create('primary ')
box.schema.space.create('5')
box.schema.space.create('')

-- valid identifiers
box.schema.space.create('_Abcde'):drop()
box.schema.space.create('_5'):drop()
box.schema.space.create('valid_identifier'):drop()
-- some OS-es ship incomplete locales, breaking ID validation
weird_chars=''
if jit.os~='OSX' and jit.os~='BSD' then weird_chars='空間' end
box.schema.space.create('ынтыпрайзный_'..weird_chars):drop() -- unicode
box.schema.space.create('utf8_наше_Фсё'):drop() -- unicode

space = box.schema.space.create('test')

-- invalid identifiers
space:create_index('invalid.identifier')
space:create_index('invalid identifier')
space:create_index('primary ')
space:create_index('5')
space:create_index('')

space:drop()
-- gh-57 Confusing error message when trying to create space with a
-- duplicate id
auto = box.schema.space.create('auto_original')
box.schema.space.create('auto', {id = auto.id})
box.schema.space.drop('auto')
box.schema.space.create('auto_original', {id = auto.id})
auto:drop()

-- ------------------------------------------------------------------
-- gh-281 Crash after rename + replace + delete with multi-part index
-- ------------------------------------------------------------------
s = box.schema.space.create('space')
index = s:create_index('primary', {unique = true, parts = {1, 'unsigned', 2, 'string'}})
s:insert{1, 'a'}
box.space.space.index.primary:rename('secondary')
box.space.space:replace{1,'The rain in Spain'}
box.space.space:delete{1,'The rain in Spain'}
box.space.space:select{}
s:drop()

-- ------------------------------------------------------------------
-- gh-362 Appropriate error messages in create_index
-- ------------------------------------------------------------------
s = box.schema.space.create(42)
s = box.schema.space.create("test", "bug")
s = box.schema.space.create("test", {unknown = 'param'})
s = box.schema.space.create("test")
index = s:create_index('primary', {unique = true, parts = {0, 'unsigned', 1, 'string'}})
index = s:create_index('primary', {unique = true, parts = {'unsigned', 1, 'string', 2}})
index = s:create_index('primary', {unique = true, parts = 'bug'})
index = s:create_index('test', {unique = true, parts = {1, 'unsigned'}, mmap = true})
s:drop()


-- ------------------------------------------------------------------
-- gh-155 Tarantool failure on simultaneous space:drop()
-- ------------------------------------------------------------------

test_run:cmd("setopt delimiter ';'")
local fiber = require('fiber')
local W = 4
local N = 50
local ch = fiber.channel(W)
for i=1,W do
    fiber.create(function()
        for k=1,N do
            local space_id = math.random(2147483647)
            local space = box.schema.space.create(string.format('space_%d', space_id))
            space:create_index('pk', { type = 'tree' })
            space:drop()
        end
        ch:put(true)
    end)
end
for i=1,W do
    ch:get()
end
test_run:cmd("setopt delimiter ''");

-- ------------------------------------------------------------------
-- Lower and upper cases
-- ------------------------------------------------------------------

space = box.schema.space.create("test")
_ = space:create_index('primary', { parts = {1, 'nUmBeR', 2, 'StRinG'}})
space.index.primary.parts[1].type == 'number'
space.index.primary.parts[2].type == 'string'
box.space._index:get({space.id, 0})[6]
space:drop()

-- ------------------------------------------------------------------
-- Aliases
-- ------------------------------------------------------------------

space = box.schema.space.create("test")
_ = space:create_index('primary', { parts = {1, 'uint', 2, 'int', 3, 'str'}})
space.index.primary.parts[1].type == 'unsigned'
space.index.primary.parts[2].type == 'integer'
space.index.primary.parts[3].type == 'string'
box.space._index:get({space.id, 0})[6]
space:drop()

-- ------------------------------------------------------------------
-- Tarantool 1.6 compatibility
-- ------------------------------------------------------------------

-- gh-1534: deprecate 'num' data type for unsigned integers
space = box.schema.space.create("test")
_ = space:create_index('primary', { parts = {1, 'num'}})
space.index.primary.parts[1].type == 'unsigned'
box.space._index:get({space.id, 0})[6]
space:drop()

-- data dictionary compatibility is checked by upgrade.test.lua

test_run:cmd("clear filter")
--
-- create_index() does not modify index options
--
s = box.schema.space.create('test', {engine='vinyl'})
opts = {parts={1, 'unsigned'}}
_ = s:create_index('pk', opts)
opts
s:drop()

--
-- gh-2074: alter a primary key
--
s = box.schema.space.create('test')
_ = s:create_index('pk')
s:insert{1, 1}
s:insert{2, 2}
s:insert{3, 3}
s.index.pk:alter({parts={1, 'num', 2, 'num'}})
s.index.pk
s:select{}
_ = s:create_index('secondary', {parts={2, 'num'}})
s.index.pk:alter({parts={1, 'num'}})
s:select{}
s.index.pk
s.index.secondary
s.index.secondary:select{}
s:drop()

--
-- Forbid explicit space id 0.
--
s = box.schema.create_space('test', { id = 0 })


--
-- gh-2660 space:truncate() does not preserve table triggers
--
ts = box.schema.space.create('test')
ti = ts:create_index('primary')

ts:insert{1, 'b', 'c'}
ts:insert{2, 'b', 'c'}

o = nil
n = nil
function save_out(told, tnew) o = told n = tnew end

_ = ts:on_replace(save_out)
ts:replace{2, 'a', 'b', 'c'}
o
n

ts:truncate()
ts:replace{2, 'a', 'b'}
o
n
ts:replace{3, 'a', 'b'}
o
n

ts:drop()

--
-- Try insert incorrect sql in index and space opts.
--
box.space._space:replace{600, 1, 'test', 'memtx', 0, { sql = 100 }, {}}

--
-- gh-2652: validate space format.
--
s = box.schema.space.create('test', { format = "format" })
format = { { name = 100 } }
s = box.schema.space.create('test', { format = format })
long = string.rep('a', box.schema.NAME_MAX + 1)
format = { { name = long } }
s = box.schema.space.create('test', { format = format })
format = { { name = 'id', type = '100' } }
s = box.schema.space.create('test', { format = format })
format = { utils.setmap({}) }
s = box.schema.space.create('test', { format = format })

-- Ensure the format is updated after index drop.
format = { { name = 'id', type = 'unsigned' } }
s = box.schema.space.create('test', { format = format })
pk = s:create_index('pk')
sk = s:create_index('sk', { parts = { 2, 'string' } })
s:replace{1, 1}
sk:drop()
s:replace{1, 1}
s:drop()

-- Check index parts conflicting with space format.
format = { { name='field1', type='unsigned' }, { name='field2', type='string' }, { name='field3', type='scalar' } }
s = box.schema.space.create('test', { format = format })
pk = s:create_index('pk')
sk1 = s:create_index('sk1', { parts = { 2, 'unsigned' } })
sk2 = s:create_index('sk2', { parts = { 3, 'number' } })

-- Check space format conflicting with index parts.
sk3 = s:create_index('sk3', { parts = { 2, 'string' } })
format[2].type = 'unsigned'
s:format(format)
s:format()
s.index.sk3.parts

-- Space format can be updated, if conflicted index is deleted.
sk3:drop()
s:format(format)
s:format()

-- Check deprecated field types.
format[2].type = 'num'
format[3].type = 'str'
format[4] = { name = 'field4', type = '*' }
format
s:format(format)
s:format()
s:replace{1, 2, '3', {4, 4, 4}}

-- Check not indexed fields checking.
s:truncate()
format[2] = {name='field2', type='string'}
format[3] = {name='field3', type='array'}
format[4] = {name='field4', type='number'}
format[5] = {name='field5', type='integer'}
format[6] = {name='field6', type='scalar'}
format[7] = {name='field7', type='map'}
format[8] = {name='field8', type='any'}
format[9] = {name='field9'}
s:format(format)

-- Check incorrect field types.
format[9] = {name='err', type='any'}
s:format(format)

s:replace{1, '2', {3, 3}, 4.4, -5, true, {value=7}, 8, 9}
s:replace{1, 2, {3, 3}, 4.4, -5, true, {value=7}, 8, 9}
s:replace{1, '2', 3, 4.4, -5, true, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, '4', -5, true, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5.5, true, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5, {6, 6}, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5, true, {7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5, true, {value=7}}
s:replace{1, '2', {3, 3}, 4.4, -5, true, {value=7}, 8}
s:truncate()

--
-- gh-1014: field names.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2'}
format[3] = {name = 'field1'}
s:format(format)

s:drop()

-- https://github.com/tarantool/tarantool/issues/2815
-- Extend space format definition syntax
format = {{name='key',type='unsigned'}, {name='value',type='string'}}
s = box.schema.space.create('test', { format = format })
s:format()
s:format({'id', 'name'})
s:format()
s:format({'id', {'name1'}})
s:format()
s:format({'id', {'name2', 'string'}})
s:format()
s:format({'id', {'name', type = 'string'}})
s:format()
s:drop()

format = {'key', {'value',type='string'}}
s = box.schema.space.create('test', { format = format })
s:format()
s:drop()

s = box.schema.space.create('test')
s:create_index('test', {parts = {'test'}})
s:create_index('test', {parts = {{'test'}}})
s:create_index('test', {parts = {{field = 'test'}}})
s:create_index('test', {parts = {1}}).parts
s:drop()

s = box.schema.space.create('test')
s:format{{'test1', 'integer'}, 'test2', {'test3', 'integer'}, {'test4','scalar'}}
s:create_index('test', {parts = {'test'}})
s:create_index('test', {parts = {{'test'}}})
s:create_index('test', {parts = {{field = 'test'}}})
s:create_index('test1', {parts = {'test1'}}).parts
s:create_index('test2', {parts = {'test2'}}).parts
s:create_index('test3', {parts = {{'test1', 'integer'}}}).parts
s:create_index('test4', {parts = {{'test2', 'integer'}}}).parts
s:create_index('test5', {parts = {{'test2', 'integer'}}}).parts
s:create_index('test6', {parts = {1, 3}}).parts
s:create_index('test7', {parts = {'test1', 4}}).parts
s:create_index('test8', {parts = {{1, 'integer'}, {'test4', 'scalar'}}}).parts
s:drop()

--
-- gh-2800: space formats checking is broken.
--

-- Ensure that vinyl correctly process field count change.
s = box.schema.space.create('test', {engine = 'vinyl', field_count = 2})
pk = s:create_index('pk')
s:replace{1, 2}
t = box.space._space:select{s.id}[1]:totable()
t[5] = 1
box.space._space:replace(t)
s:drop()

-- Check field type changes.
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'any'}
format[3] = {name = 'field3', type = 'unsigned'}
format[4] = {name = 'field4', type = 'string'}
format[5] = {name = 'field5', type = 'number'}
format[6] = {name = 'field6', type = 'integer'}
format[7] = {name = 'field7', type = 'boolean'}
format[8] = {name = 'field8', type = 'scalar'}
format[9] = {name = 'field9', type = 'array'}
format[10] = {name = 'field10', type = 'map'}
s = box.schema.space.create('test', {format = format})
pk = s:create_index('pk')
t = s:replace{1, 2, 3, '4', 5.5, -6, true, 8, {9, 9}, {val = 10}}

test_run:cmd("setopt delimiter ';'")
function fail_format_change(fieldno, new_type)
    local old_type = format[fieldno].type
    format[fieldno].type = new_type
    local ok, msg = pcall(s.format, s, format)
    format[fieldno].type = old_type
    return msg
end;

function ok_format_change(fieldno, new_type)
    local old_type = format[fieldno].type
    format[fieldno].type = new_type
    s:format(format)
    s:delete{1}
    format[fieldno].type = old_type
    s:format(format)
    s:replace(t)
end;
test_run:cmd("setopt delimiter ''");

-- any --X--> unsigned
fail_format_change(2, 'unsigned')

-- unsigned -----> any
ok_format_change(3, 'any')
-- unsigned --X--> string
fail_format_change(3, 'string')
-- unsigned -----> number
ok_format_change(3, 'number')
-- unsigned -----> integer
ok_format_change(3, 'integer')
-- unsigned -----> scalar
ok_format_change(3, 'scalar')
-- unsigned --X--> map
fail_format_change(3, 'map')

-- string -----> any
ok_format_change(4, 'any')
-- string -----> scalar
ok_format_change(4, 'scalar')
-- string --X--> boolean
fail_format_change(4, 'boolean')

-- number -----> any
ok_format_change(5, 'any')
-- number -----> scalar
ok_format_change(5, 'scalar')
-- number --X--> integer
fail_format_change(5, 'integer')

-- integer -----> any
ok_format_change(6, 'any')
-- integer -----> number
ok_format_change(6, 'number')
-- integer -----> scalar
ok_format_change(6, 'scalar')
-- integer --X--> unsigned
fail_format_change(6, 'unsigned')

-- boolean -----> any
ok_format_change(7, 'any')
-- boolean -----> scalar
ok_format_change(7, 'scalar')
-- boolean --X--> string
fail_format_change(7, 'string')

-- scalar -----> any
ok_format_change(8, 'any')
-- scalar --X--> unsigned
fail_format_change(8, 'unsigned')

-- array -----> any
ok_format_change(9, 'any')
-- array --X--> scalar
fail_format_change(9, 'scalar')

-- map -----> any
ok_format_change(10, 'any')
-- map --X--> scalar
fail_format_change(10, 'scalar')

s:drop()

-- Check new fields adding.
format = {}
s = box.schema.space.create('test')
format[1] = {name = 'field1', type = 'unsigned'}
s:format(format) -- Ok, no indexes.
pk = s:create_index('pk')
format[2] = {name = 'field2', type = 'unsigned'}
s:format(format) -- Ok, empty space.
s:replace{1, 1}
format[2] = nil
s:format(format) -- Ok, can delete fields with no checks.
s:delete{1}
sk1 = s:create_index('sk1', {parts = {2, 'unsigned'}})
sk2 = s:create_index('sk2', {parts = {3, 'unsigned'}})
sk5 = s:create_index('sk5', {parts = {5, 'unsigned'}})
s:replace{1, 1, 1, 1, 1}
format[2] = {name = 'field2', type = 'unsigned'}
format[3] = {name = 'field3', type = 'unsigned'}
format[4] = {name = 'field4', type = 'any'}
format[5] = {name = 'field5', type = 'unsigned'}
-- Ok, all new fields are indexed or have type ANY, and new
-- field_count <= old field_count.
s:format(format)

s:replace{1, 1, 1, 1, 1, 1}
format[6] = {name = 'field6', type = 'unsigned'}
-- Ok, but check existing tuples for a new field[6].
s:format(format)

-- Fail, not enough fields.
s:replace{2, 2, 2, 2, 2}

s:replace{2, 2, 2, 2, 2, 2, 2}
format[7] = {name = 'field7', type = 'unsigned'}
-- Fail, the tuple {1, ... 1} is invalid for a new format.
s:format(format)
s:drop()

-- Vinyl does not support adding fields to a not empty space.
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:replace{1,1}
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned'}
s:format(format)
s:drop()

--
-- gh-1557: NULL in indexes.
--

NULL = require('msgpack').NULL

format = {}
format[1] = { name = 'field1', type = 'unsigned', is_nullable = true }
format[2] = { name = 'field2', type = 'unsigned', is_nullable = true }
s = box.schema.space.create('test', { format = format })
s:create_index('primary', { parts = { 'field1' } })
s:create_index('primary', { parts = {{'field1', is_nullable = false}} })
format[1].is_nullable = false
s:format(format)
s:create_index('primary', { parts = {{'field1', is_nullable = true}} })

s:create_index('primary', { parts = {'field1'} })

-- Check that is_nullable can't be set to false on non-empty space
s:insert({1, NULL})
format[1].is_nullable = true
s:format(format)
format[1].is_nullable = false
format[2].is_nullable = false
s:format(format)
s:delete(1)
-- Disable is_nullable on empty space
s:format(format)
s:format({})

s:create_index('secondary', { parts = {{2, 'string', is_nullable = true}} })
s:insert({1, NULL})
s.index.secondary:alter({ parts = {{2, 'string', is_nullable = false} }})
s:delete({1})
s.index.secondary:alter({ parts = {{2, 'string', is_nullable = false} }})
s:insert({1, NULL})
s:insert({2, 'xxx'})
s.index.secondary:alter({ parts = {{2, 'string', is_nullable = true} }})
s:insert({1, NULL})

s:drop()

s = box.schema.create_space('test')
test_run:cmd("setopt delimiter ';'")
s:format({
    [1] = { name = 'id1', type = 'unsigned'},
    [2] = { name = 'id2', type = 'unsigned'},
    [3] = { name = 'id3', type = 'string'},
    [4] = { name = 'id4', type = 'string'},
    [5] = { name = 'id5', type = 'string'},
    [6] = { name = 'id6', type = 'string'},
});
test_run:cmd("setopt delimiter ''");
s:format()
_ = s:create_index('primary')
s:insert({1, 1, 'a', 'b', 'c', 'd'})
s:drop()

s = box.schema.create_space('test')
idx = s:create_index('idx')
box.space.test == s
s:drop()
