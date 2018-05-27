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
_index:insert{_space.id, 0, 'primary', 'tree', 1, 1, 0, 'unsigned'}
_index:replace{_space.id, 0, 'primary', 'tree', 1, 1, 0, 'unsigned'}
_index:insert{_index.id, 0, 'primary', 'tree', 1, 2, 0, 'unsigned', 1, 'unsigned'}
_index:replace{_index.id, 0, 'primary', 'tree', 1, 2, 0, 'unsigned', 1, 'unsigned'}
-- access_sysview.test changes output of _index:select{}.
-- let's change _index space in such a way that it will be
-- uniformn weather access_sysview.test is completed of not.
box.space._space.index.owner:alter{parts = {2, 'unsigned'}}
box.space._vspace.index.owner:alter{parts = {2, 'unsigned'}}
_index:select{}
-- modify indexes of a system space
_index:delete{_index.id, 0}
_space:insert{1000, ADMIN, 'hello', 'memtx', 0, EMPTY_MAP, {}}
_index:insert{1000, 0, 'primary', 'tree', 1, 1, 0, 'unsigned'}
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
--
-- The result of this test is superseded by the change made
-- in scope of gh-2914, which allows all printable characters for
-- identifiers.
--
--------------------------------------------------------------------------------

-- invalid identifiers
s = box.schema.space.create('invalid.identifier')
s.name
s:drop()
s = box.schema.space.create('invalid identifier')
s.name
s:drop()
s = box.schema.space.create('primary ')
'|'..s.name..'|'
s:drop()
s = box.schema.space.create('5')
s.name
s:drop()
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
i = space:create_index('invalid.identifier')
i.name
i:drop()
i = space:create_index('invalid identifier')
i.name
i:drop()
i = space:create_index('primary ')
'|'..i.name..'|'
i:drop()
i = space:create_index('5')
i.name
i:drop()
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
-- gh-2914: Allow any space name which consists of printable characters
--
identifier = require("identifier")
test_run:cmd("setopt delimiter ';'")
identifier.run_test(
	function (identifier)
		box.schema.space.create(identifier)
		if box.space[identifier] == nil then
			error("Cannot query space")
		end
	end,
	function (identifier) box.space[identifier]:drop() end
);

s = box.schema.create_space("test");
identifier.run_test(
    function (identifier) s:create_index(identifier, {parts={1}}) end,
    function (identifier) s.index[identifier]:drop() end
);
s:drop();

--
-- gh-2914: check column name validation.
-- Ensure that col names are validated as identifiers.
--
s = box.schema.create_space('test');
i = s:create_index("primary", {parts={1, "integer"}});
identifier.run_test(
	function (identifier)
		s:format({{name=identifier,type="integer"}})
		local t = s:replace({1})
		if t[identifier] ~= 1 then
			error("format identifier error")
		end
	end,
	function (identifier) end
);
s:drop();

-- gh-2914: check coll name validation.
identifier.run_test(
    function (identifier) box.internal.collation.create(identifier, 'ICU', 'ru-RU', {}) end,
    function (identifier) box.internal.collation.drop(identifier) end
);

test_run:cmd("setopt delimiter ''");

--
-- gh-3011: add new names to old tuple formats.
--
s = box.schema.create_space('test')
pk = s:create_index('pk')
t1 = s:replace{1}
t1.field1
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
s:format(format)
t2 = s:replace{2}
t2.field1
t1.field1
format[1].name = 'field_1'
s:format(format)
t3 = s:replace{3}
t1.field1
t1.field_1
t2.field1
t2.field_1
t3.field1
t3.field_1
s:drop()

--
-- gh-3008. Ensure the change of hash index parts updates hash
-- key_def.
--
s = box.schema.create_space('test')
pk = s:create_index('pk', {type = 'hash'})
pk:alter{parts = {{1, 'string'}}}
s:replace{'1', '1'}
s:replace{'1', '2'}
pk:select{}
pk:select{'1'}
s:drop()

--
-- Ensure that incompatible key parts change validates format.
--
s = box.schema.create_space('test')
pk = s:create_index('pk')
s:replace{1}
pk:alter{parts = {{1, 'string'}}} -- Must fail.
s:drop()

--
-- gh-2895: do not ignore field type in space format, if it is not
-- specified via 'type = ...'.
--
format = {}
format[1] = {name = 'field1', 'unsigned'}
format[2] = {name = 'field2', 'unsigned'}
s = box.schema.create_space('test', {format = format})
s:format()

format[2] = {name = 'field2', 'unsigned', 'unknown'}
s:format(format)
s:format()

s:drop()

--
-- gh-3285: index Lua object is not updated on alter.
--
box.internal.collation.create('test', 'ICU', 'ru-RU')
s = box.schema.create_space('test')
pk = s:create_index('pk')
sk1 = s:create_index('b', {unique = false})
sk2 = s:create_index('c', {type = 'hash', parts = {{3, 'unsigned'}}})
sk3 = s:create_index('d', {parts = {{4, 'string'}}})
test_run:cmd("setopt delimiter ';'")
function index_count()
    local r = 0
    for i, _ in pairs(s.index) do
        r = r + 1
    end
    return r / 2
end;
function validate_indexes()
    assert(s == box.space.test)
    assert(sk1 == nil or sk1 == s.index[sk1.name])
    assert(sk2 == nil or sk2 == s.index[sk2.name])
    assert(sk3 == nil or sk3 == s.index[sk3.name])
end;
test_run:cmd("setopt delimiter ''");
index_count()

sk3:rename('dd')
sk3.name == 'dd'
index_count()
validate_indexes()

sk3:alter({parts = {{4, 'string', collation = 'test'}}})
sk3.parts[1].collation == 'test'
index_count()
validate_indexes()

sk3:drop()
sk3 = nil
index_count()
validate_indexes()

sk2:alter({parts = {{4, 'string'}}})
sk2.parts[1].type == 'string'
sk2.parts[1].fieldno == 4
index_count()
validate_indexes()

sk1:alter({unique = true})
sk1.unique
index_count()
validate_indexes()

s:drop()
box.internal.collation.drop('test')
