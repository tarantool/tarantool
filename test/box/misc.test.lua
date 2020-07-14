env = require('test_run')
test_run = env.new()
test_run:cmd("push filter 'table: .*' to 'table: <address>'")

-- gh-266: box.info() crash on uncofigured box
package.loaded['box.space'] == nil
package.loaded['box.index'] == nil
package.loaded['box.tuple'] == nil
package.loaded['box.error'] == nil
package.loaded['box.info'] == nil
package.loaded['box.stat'] == nil
package.loaded['box.session'] == nil

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

-- Test Lua from admin console. Whenever producing output,
-- make sure it's a valid YAML.
'  lua says: hello'

--
-- gh-3308: feedback daemon is an optional pre-compile time
-- defined feature, depending on CMake flags. It is not present
-- always.
--
optional = {feedback = true}
-- # What's in the box?
t = {}
for n in pairs(box) do                                                          \
    if not optional[n] then                                                     \
        table.insert(t, tostring(n))                                            \
    end                                                                         \
end                                                                             \
table.sort(t)
t
t = nil

----------------
-- # box.stat
----------------
t = {}
test_run:cmd("setopt delimiter ';'")
for k, v in pairs(box.stat()) do
    table.insert(t, k)
end;
for k, v in pairs(box.stat().DELETE) do
    table.insert(t, k)
end;
for k, v in pairs(box.stat.DELETE) do
    table.insert(t, k)
end;
t;

----------------
-- # box.space
----------------
type(box);
type(box.space);
t = {};
for i, v in pairs(space.index[0].parts[1]) do
    table.insert(t, tostring(i)..' : '..tostring(v))
end;
t;

----------------
-- # box.slab
----------------
string.match(tostring(box.slab.info()), '^table:') ~= nil;
box.slab.info().arena_used >= 0;
box.slab.info().arena_size > 0;
string.match(tostring(box.slab.stats()), '^table:') ~= nil;
t = {};
for k, v in pairs(box.slab.info()) do
    table.insert(t, k)
end;
t;
box.runtime.info().used > 0;
box.runtime.info().maxalloc > 0;

--
-- gh-502: box.slab.info() excessively sparse array
--
type(require('yaml').encode(box.slab.info()));

test_run:cmd("setopt delimiter ''");

-- A test case for Bug#901674
-- No way to inspect exceptions from Box in Lua
--
function myinsert(tuple) box.space.tweedledum:insert(tuple) end
pcall(myinsert, {99, 1, 1953719668})
pcall(myinsert, {1, 'hello'})
pcall(myinsert, {1, 'hello'})
box.space.tweedledum:truncate()
myinsert = nil

-- A test case for gh-37: print of 64-bit number

ffi = require('ffi')
1, 1
tonumber64(1), 1

-- Testing 64bit
tonumber64()
tonumber64('invalid number')
tonumber64(123)
tonumber64('123')
type(tonumber64('4294967296')) == 'number'
tonumber64('9223372036854775807') == tonumber64('9223372036854775807')
tonumber64('9223372036854775807') - tonumber64('9223372036854775800')
tonumber64('18446744073709551615') == tonumber64('18446744073709551615')
tonumber64('18446744073709551615') + 1
tonumber64(-1)
tonumber64('184467440737095516155')
string.byte(require('msgpack').encode(tonumber64(123)))
--  A test case for Bug#1061747 'tonumber64 is not transitive'
tonumber64(tonumber64(2))
tostring(tonumber64(tonumber64(3)))
--  A test case for Bug#1131108 'tonumber64 from negative int inconsistency'
tonumber64(-1)
tonumber64(-1LL)
tonumber64(-1ULL)
-1
-1LL
-1ULL
tonumber64(-1.0)
6LL - 7LL
tostring(tonumber64('1234567890123')) == '1234567890123'
tostring(tonumber64('12345678901234')) == '12345678901234'
tostring(tonumber64('123456789012345')) == '123456789012345ULL'
tostring(tonumber64('1234567890123456')) == '1234567890123456ULL'

--
-- gh-3466: Strange behaviour of tonumber64 function
--
tostring(tonumber64('9223372036854775807')) == '9223372036854775807ULL'
tostring(tonumber64('18446744073709551615')) == '18446744073709551615ULL'
tonumber64('18446744073709551616') == nil
tostring(tonumber64('-9223372036854775808')) == '-9223372036854775808LL'
tonumber64('-9223372036854775809') == nil
tostring(tonumber64('0')) == '0'

--
-- gh-3431: tonumber of strings with ULL.
--
tonumber64('-1ULL') == -1ULL
tonumber64('-1LL') == -1LL
tonumber64('12345678910ULL') == 12345678910ULL
tonumber64(tostring(tonumber64('1234567890123456'))) == 1234567890123456ULL

tonumber64('0x12') == 18
tonumber64('0x12', 16) == 18
tonumber64('0x12', 17) == nil
tonumber64('0b01') == 1
tonumber64('0b01', 2) == 1
tonumber64('0b01', 3) == nil
tonumber64('  0b1  ') == 1
tonumber64('  0b1  ', 'badbase')
tonumber64('  0b1  ', 123) -- big base
tonumber64('12345', 123) -- big base
tonumber64('0xfffff') == 1048575
tonumber64('0b111111111111111111') == 262143

tonumber64('20', 36)

tonumber64("", 10)
tonumber64("", 32)

tonumber64("-1")
tonumber64("-0x16")
tonumber64("-0b11")
tonumber64(" -0x16 ")
tonumber64(" -0b11 ")

-- numbers/cdata with base = 10 - return as is
tonumber64(100)
tonumber64(100, 10)
tonumber64(100LL)
tonumber64(100ULL, 10)
tonumber64(-100LL)
tonumber64(-100LL, 10)
tonumber64(ffi.new('char', 10))
tonumber64(ffi.new('short', 10))
tonumber64(ffi.new('int', 10))
tonumber64(ffi.new('int8_t', 10))
tonumber64(ffi.new('int16_t', 10))
tonumber64(ffi.new('int32_t', 10))
tonumber64(ffi.new('int64_t', 10))
tonumber64(ffi.new('unsigned char', 10))
tonumber64(ffi.new('unsigned short', 10))
tonumber64(ffi.new('unsigned int', 10))
tonumber64(ffi.new('unsigned int', 10))
tonumber64(ffi.new('uint8_t', 10))
tonumber64(ffi.new('uint16_t', 10))
tonumber64(ffi.new('uint32_t', 10))
tonumber64(ffi.new('uint64_t', 10))
tonumber64(ffi.new('float', 10))
tonumber64(ffi.new('double', 10))

-- number/cdata with custom `base` - is not supported
tonumber64(100, 2)
tonumber64(100LL, 2)
tonumber64(-100LL, 2)
tonumber64(100ULL, 2)
tonumber64(ffi.new('char', 10), 2)
tonumber64(ffi.new('short', 10), 2)
tonumber64(ffi.new('int', 10), 2)
tonumber64(ffi.new('int8_t', 10), 2)
tonumber64(ffi.new('int16_t', 10), 2)
tonumber64(ffi.new('int32_t', 10), 2)
tonumber64(ffi.new('int64_t', 10), 2)
tonumber64(ffi.new('unsigned char', 10), 2)
tonumber64(ffi.new('unsigned short', 10), 2)
tonumber64(ffi.new('unsigned int', 10), 2)
tonumber64(ffi.new('unsigned int', 10), 2)
tonumber64(ffi.new('uint8_t', 10), 2)
tonumber64(ffi.new('uint16_t', 10), 2)
tonumber64(ffi.new('uint32_t', 10), 2)
tonumber64(ffi.new('uint64_t', 10), 2)
tonumber64(ffi.new('float', 10), 2)
tonumber64(ffi.new('double', 10), 2)

-- invalid types - return nil
ffi.cdef("struct __tonumber64_test {};")
tonumber64(ffi.new('struct __tonumber64_test'))
tonumber64(nil)
tonumber64(function() end)
tonumber64({})

collectgarbage('collect')

--  dostring()
dostring('abc')
dostring('abc=2')
dostring('return abc')
dostring('return ...', 1, 2, 3)
--  A test case for Bug#1043804 lua error() -> server crash
error()
--  A test case for bitwise operations
bit.lshift(1, 32)
bit.band(1, 3)
bit.bor(1, 2)

space:truncate()

fifo = require('fifo')
fifo.fifomax
fifo.fifo_push(space, 1, 1)
fifo.fifo_push(space, 1, 2)
fifo.fifo_push(space, 1, 3)
fifo.fifo_push(space, 1, 4)
fifo.fifo_push(space, 1, 5)
fifo.fifo_push(space, 1, 6)
fifo.fifo_push(space, 1, 7)
fifo.fifo_push(space, 1, 8)
fifo.fifo_top(space, 1)
space:delete{1}
fifo.fifo_top(space, 1)
space:delete{1}

space:drop()
test_run:cmd("clear filter")

-- test test_run:grep_log()
require('log').info('Incorrect password supplied')
test_run:grep_log("default", "password")

-- some collation test
s = box.schema.space.create('test')
not not s:create_index('test1', {parts = {{1, 'string', collation = 'Unicode'}}})
not not s:create_index('test2', {parts = {{2, 'string', collation = 'UNICODE'}}})
not not s:create_index('test3', {parts = {{3, 'string', collation = 'UnIcOdE'}}}) -- I'd prefer to panic on that
s:create_index('test4', {parts = {{4, 'string'}}}).parts
s:create_index('test5', {parts = {{5, 'string', collation = 'Unicode'}}}).parts
s:drop()

s = box.schema.space.create('test')
not not s:create_index('test1', {parts = {{1, 'scalar', collation = 'unicode_ci'}}})
s:replace{1} s:replace{1.1} s:replace{false}
s:replace{'Блин'} s:replace{'Ёж'} s:replace{'ешь'} s:replace{'Же'} s:replace{'Уже'}
s:replace{'drop'} s:replace{'table'} s:replace{'users'}
s:select{}
s:select{'еж'}
s:drop()

s = box.schema.space.create('test')
not not s:create_index('test1', {parts = {{1, 'number', collation = 'unicode_ci'}}})
not not s:create_index('test2', {parts = {{2, 'unsigned', collation = 'unicode_ci'}}})
not not s:create_index('test3', {parts = {{3, 'integer', collation = 'unicode_ci'}}})
not not s:create_index('test4', {parts = {{4, 'boolean', collation = 'unicode_ci'}}})
s:drop()

--
-- gh-2068 no error for invalid user during space creation
--
s = box.schema.space.create('test', {user="no_such_user"})

--
-- gh-3659 assertion failure after an error in code called from
-- box.session.su()
--
box.session.su("admin", function(x) return #x end, 3)

-- Too long WAL write warning (gh-2743).
s = box.schema.space.create('test')
_ = s:create_index('pk')
too_long_threshold = box.cfg.too_long_threshold
box.cfg{too_long_threshold = 0} -- log everything
expected_rows = 3
expected_lsn = box.info.lsn + 1
box.begin() for i = 1, expected_rows do s:insert{i} end box.commit()
msg = test_run:grep_log('default', 'too long WAL write.*')
rows, lsn = string.match(msg, '(%d+) rows at LSN (%d+)')
rows = tonumber(rows)
lsn = tonumber(lsn)
rows == expected_rows
lsn == expected_lsn
box.cfg{too_long_threshold = too_long_threshold}
s:drop()

--
-- gh-2978: Function to parse space format.
-- In next tests we should receive cdata("struct tuple_format *").
-- We do not have a way to check cdata in Lua, but there should be
-- no errors.
--

-- Without argument it is equivalent to new_tuple_format({})
tuple_format = box.internal.new_tuple_format()

-- If no type that type == "any":
format = {}
format[1] = {}
format[1].name = 'aaa'
tuple_format = box.internal.new_tuple_format(format)

-- Function space:format() without arguments returns valid format:
tuple_format = box.internal.new_tuple_format(box.space._space:format())

-- Check is_nullable option fo field
format[1].is_nullable = true
tuple_format = box.internal.new_tuple_format(format)

--
-- Test that calling _say using FFI w/ null filepointer doesn't
-- segfault
--
box.cfg{}
local ffi = require'ffi' ffi.C._say(ffi.C.S_WARN, nil, 0, nil, "%s", "test log")
test_run:grep_log('default', 'test log')

--
-- gh-2866: one more way to declare index parts
--
s = box.schema.space.create('test')
i = s:create_index('test1', {parts = {{1, 'unsigned'}}})
i = s:create_index('test2', {parts = {{2, 'string', is_nullable = true, collation = 'unicode'}}})
i.parts
i = s:create_index('test4', {parts = {3, 'string', is_nullable = true}})
i.parts
i = s:create_index('test5', {parts = {3, 'string', collation = 'unicode'}})
i.parts
i = s:create_index('test6', {parts = {4, 'string'}})
i.parts
s:drop()

--
-- gh-5473: forbid specifying index options in key parts
--
--
s = box.schema.space.create('test')
i = s:create_index('test1', {parts = {1, 'unsigned', unique=false}})
i = s:create_index('test1', {parts = {1, 'unsigned', distance=3}})
i = s:create_index('test1', {parts = {2, 'string', 3, 'string', unique=false}})
i = s:create_index('test1', {parts = {2, 'string', 3, 'string', distance=3}})
i = s:create_index('test1', {parts = {{1,'int', distance=3}, {field=2, type='int'}}})
i = s:create_index('test1', {parts = {{1,'int'}, {field=2, type='int', type='hash'}}})
i = s:create_index('test1', {parts = {{1,'int'}, {2, field=2, type='int'}}})
i = s:create_index('test1', {parts = {{1,'int'}, {field=2, type='int', 'asd'}}})
i = s:create_index('test1', {parts = {1,'int', 'asd'}})
i = s:create_index('test1', {parts = {{1, 'int'}, {2, 'int', 'asd'}}})
i = s:create_index('test1', {parts = {{2, type='unsigned', 'asd'}, {1, 'int'}}})
i = s:create_index('test1', {parts = {{1, 'int'}, {2, 'asd', type='unsigned'}}})
i = s:create_index('test1', {parts = {{'asd', 2, type='unsigned'}}})
i = s:create_index('test1', {parts = {{1, 'int'}, {2, type='asd'}}})
s:drop()
