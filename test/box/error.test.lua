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
