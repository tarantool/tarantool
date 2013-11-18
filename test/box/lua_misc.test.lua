space = box.schema.create_space('tweedledum')
space:create_index('primary', 'hash', { parts = { 0, 'num' }})

----------------
-- # box.raise
----------------
1 + 1
box.raise(123, 'test')
box.raise(0, 'the other test')
box.raise(12, 345)

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
box.cfg.memcached_space;
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

space:drop();
--# setopt delimiter ''
-- A test case for gh-37: print of 64-bit number
1, 1
tonumber64(1), 1
