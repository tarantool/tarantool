space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })

-- Test Lua from admin console. Whenever producing output,
-- make sure it's a valid YAML.
'  lua says: hello'
-- # What's in the box?
t = {} for n in pairs(box) do table.insert(t, tostring(n)) end table.sort(t)
t
t = nil

----------------
-- # box.raise
----------------

--# stop server default
--# start server default
-- do nothing - no exceptions were thrown before
box.raise()

1 + 1
box.raise(123, 'test')
box.raise(0, 'the other test')
box.raise(12, 345)
box.raise()
box.raise()

space = box.space.tweedledum

----------------
-- # box.stat
----------------
t = {}
--# setopt delimiter ';'
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
for i, v in pairs(space.index[0].key_field[0]) do
    table.insert(t, tostring(i)..' : '..tostring(v))
end;
t;

----------------
-- # box.space
----------------
string.match(tostring(box.slab.info()), '^table:') ~= nil;
box.slab.info().arena_used >= 0;
box.slab.info().arena_size > 0;
string.match(tostring(box.slab.info().slabs), '^table:') ~= nil;
t = {};
for k, v in pairs(box.slab.info()) do
    table.insert(t, k)
end;
t;

----------------
-- # box.error
----------------
t = {}
for k,v in pairs(box.error) do
   table.insert(t, 'box.error.'..tostring(k)..' : '..tostring(v))
end;
t;

--# setopt delimiter ''

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
1, 1
tonumber64(1), 1

-- Testing 64bit
tonumber64(123)
tonumber64('123')
type(tonumber64('123')) == 'cdata'
tonumber64('9223372036854775807') == tonumber64('9223372036854775807')
tonumber64('9223372036854775807') - tonumber64('9223372036854775800')
tonumber64('18446744073709551615') == tonumber64('18446744073709551615')
tonumber64('18446744073709551615') + 1
tonumber64(-1)
tonumber64('184467440737095516155')
string.byte(box.pack('p', tonumber64(123)))
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

--  box.dostring()
box.dostring('abc')
box.dostring('abc=2')
box.dostring('return abc')
box.dostring('return ...', 1, 2, 3)
--  A test case for Bug#1043804 lua error() -> server crash
error()
--  A test case for bitwise operations 
bit.lshift(1, 32)
bit.band(1, 3)
bit.bor(1, 2)

-- A test case for box.counter
space = box.space.tweedledum
box.counter.inc(space.n, {1})
space:select{1}
box.counter.inc(space.n, {1})
box.counter.inc(space.n, {1})
space:select{1}
box.counter.dec(space.n, {1})
box.counter.dec(space.n, {1})
box.counter.dec(space.n, {1})
space:select{1}
space:truncate()

dofile('fifo.lua')
fifomax
fifo_push(space, 1, 1)
fifo_push(space, 1, 2)
fifo_push(space, 1, 3)
fifo_push(space, 1, 4)
fifo_push(space, 1, 5)
fifo_push(space, 1, 6)
fifo_push(space, 1, 7)
fifo_push(space, 1, 8)
fifo_top(space, 1)
space:delete{1}
fifo_top(space, 1)
space:delete{1}

space:drop()
