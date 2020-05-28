env = require('test_run')
test_run = env.new()

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

box.error({code = 123, reason = 'test'})
box.error(box.error.ILLEGAL_PARAMS, "bla bla")
box.error()
e = box.error.last()
e
e:unpack()
e.type
e.code
e.message
tostring(e)
e = nil
box.error.clear()
box.error.last()
space = box.space.tweedledum

--
-- gh-2080: box.error() crashes with wrong parameters
box.error(box.error.UNSUPPORTED, "x", "x%s")
box.error(box.error.UNSUPPORTED, "x")
box.error(box.error.UNSUPPORTED)

--
-- gh-3031: allow to create an error object with no throwing it.
--
e = box.error.new(box.error.UNKNOWN)
e
e = box.error.new(box.error.CREATE_SPACE, "space", "error")
e
box.error.new()

--
-- gh-4489: box.error has __concat metamethod
--
test_run:cmd("push filter '(.builtin/.*.lua):[0-9]+' to '\\1'")
e = box.error.new(box.error.UNKNOWN)
'left side: ' .. e
e .. ': right side'
e .. nil
nil .. e
e .. box.NULL
box.NULL .. e
123 .. e
e .. 123
e .. e
e .. {}
{} .. e
-1ULL .. e
e .. -1ULL
1LL .. e
e .. 1LL
e = nil

--
-- System errors expose errno as a field.
--
_, err = require('fio').open('not_existing_file')
type(err.errno)
-- Errors not related to the standard library do
-- not expose errno.
err = box.error.new(box.error.PROC_LUA, "errno")
type(err.errno)

t = {}
test_run:cmd("setopt delimiter ';'")

for k,v in pairs(box.error) do
   if type(v) == 'number' then
    t[v] = 'box.error.'..tostring(k)
   end
end;
t;

test_run:cmd("setopt delimiter ''");

-- gh-4778: don't add created via box.error.new() errors to
-- Tarantool's diagnostic area.
--
err = box.error.new({code = 111, reason = "cause"})
assert(box.error.last() ~= err)
box.error.set(err)
assert(box.error.last() == err)
-- Consider wrong or tricky inputs to box.error.set()
--
box.error.set(1)
box.error.set(nil)
box.error.set(box.error.last())
assert(box.error.last() == err)
-- Check that box.error.new() does not set error to diag.
--
box.error.clear()
err = box.error.new(1, "cause")
assert(box.error.last() == nil)

-- box.error.new() does not accept error objects.
--
box.error.new(err)

-- box.error() is supposed to re-throw last diagnostic error.
-- Make sure it does not fail if there's no errors at all
-- (in diagnostics area).
--
box.error.clear()
box.error()

space:drop()

-- gh-1148: errors can be arranged into list (so called
-- stacked diagnostics).
--
e1 = box.error.new({code = 111, reason = "cause"})
assert(e1.prev == nil)
e1:set_prev(e1)
assert(e1.prev == nil)
e2 = box.error.new({code = 111, reason = "cause of cause"})
e1:set_prev(e2)
assert(e1.prev == e2)
e2:set_prev(e1)
assert(e2.prev == nil)
-- At this point stack is following: e1 -> e2
-- Let's test following cases:
-- 1. e3 -> e2, e1 -> NULL (e3:set_prev(e2))
-- 2. e1 -> e3, e2 -> NULL (e1:set_prev(e3))
-- 3. e3 -> e1 -> e2 (e3:set_prev(e1))
-- 4. e1 -> e2 -> e3 (e2:set_prev(e3))
--
e3 = box.error.new({code = 111, reason = "another cause"})
e3:set_prev(e2)
assert(e3.prev == e2)
assert(e2.prev == nil)
assert(e1.prev == nil)

-- Reset stack to e1 -> e2 and test case 2.
--
e1:set_prev(e2)
assert(e2.prev == nil)
assert(e3.prev == nil)
e1:set_prev(e3)
assert(e2.prev == nil)
assert(e1.prev == e3)
assert(e3.prev == nil)

-- Reset stack to e1 -> e2 and test case 3.
--
e1:set_prev(e2)
assert(e1.prev == e2)
assert(e2.prev == nil)
assert(e3.prev == nil)
e3:set_prev(e1)
assert(e1.prev == e2)
assert(e2.prev == nil)
assert(e3.prev == e1)

-- Unlink errors and test case 4.
--
e1:set_prev(nil)
e2:set_prev(nil)
e3:set_prev(nil)
e1:set_prev(e2)
e2:set_prev(e3)
assert(e1.prev == e2)
assert(e2.prev == e3)
assert(e3.prev == nil)

-- Test circle detecting. At the moment stack is
-- following: e1 -> e2 -> e3
--
e3:set_prev(e1)
assert(e3.prev == nil)
e3:set_prev(e2)
assert(e3.prev == nil)

-- Test splitting list into two ones.
-- After that we will get two lists: e1->e2->e5 and e3->e4
--
e4 = box.error.new({code = 111, reason = "yet another cause"})
e5 = box.error.new({code = 111, reason = "and another one"})
e3:set_prev(e4)
e2:set_prev(e5)
assert(e1.prev == e2)
assert(e2.prev == e5)
assert(e3.prev == e4)
assert(e5.prev == nil)
assert(e4.prev == nil)

-- Another splitting option: e1->e2 and e5->e3->e4
-- But firstly restore to one single list e1->e2->e3->e4
--
e2:set_prev(e3)
e5:set_prev(e3)
assert(e1.prev == e2)
assert(e2.prev == nil)
assert(e5.prev == e3)
assert(e3.prev == e4)
assert(e4.prev == nil)

-- In case error is destroyed, it unrefs reference counter
-- of its previous error. In turn, box.error.clear() refs/unrefs
-- only head and doesn't touch other errors.
--
e2:set_prev(nil)
box.error.set(e1)
assert(box.error.last() == e1)
assert(box.error.last().prev == e2)
box.error.clear()
assert(box.error.last() == nil)
assert(e1.prev == e2)
assert(e2.code == 111)
box.error.set(e1)
box.error.clear()
assert(e1.prev == e2)

-- Set middle of an error stack into the diagnostics area.
e1:set_prev(e2)
e2:set_prev(e3)
box.error.set(e2)
assert(e1.prev == nil)
assert(e2.prev == e3)

-- gh-4829: always promote error created via box.error() to
-- diagnostic area.
e1 = box.error.new({code = 111, reason = "cause"})
box.error({code = 111, reason = "err"})
box.error.last()
box.error(e1)
assert(box.error.last() == e1)
--
-- gh-4398: custom error type.
--
-- Try no code.
e = box.error.new({type = 'TestType', reason = 'Test reason'})
e:unpack()
-- Try code not the same as used by default.
e = box.error.new({type = 'TestType', reason = 'Test reason', code = 123})
e:unpack()
-- Try to omit message.
e = box.error.new({type = 'TestType'})
e:unpack()
-- Try too long type name.
e = box.error.new({type = string.rep('a', 128)})
#e.type

-- gh-4887: accessing 'prev' member also refs it so that after
-- error is gone, its 'prev' is staying alive.
--
lua_code = [[function(tuple) local json = require('json') return json.encode(tuple) end]]
box.schema.func.create('runtimeerror', {body = lua_code, is_deterministic = true, is_sandboxed = true})
s = box.schema.space.create('withdata')
pk = s:create_index('pk')
idx = s:create_index('idx', {func = box.func.runtimeerror.id, parts = {{1, 'string'}}})

function test_func() return pcall(s.insert, s, {1}) end
ok, err = test_func()
preve = err.prev
gc_err = setmetatable({preve}, {__mode = 'v'})
err:set_prev(nil)
err.prev
collectgarbage('collect')
--  Still one reference to err.prev so it should not be collected.
--
gc_err
preve = nil
collectgarbage('collect')
gc_err

s:drop()
box.schema.func.drop('runtimeerror')

-- gh-4903: add format string usage for a CustomError message
--
err = box.error.new('TestType', 'Message arg1: %s. Message arg2: %u', '1', 2)
err.message
