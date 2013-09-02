box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

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
for i, v in pairs(box.space[0].index[0].key_field[0]) do
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

box.space[0]:drop();
