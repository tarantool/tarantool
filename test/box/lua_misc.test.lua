-- setopt delim ';'
----------------
-- # box.raise
----------------;
1 + 1;
box.raise(123, 'test');
box.raise(0, 'the other test');
box.raise(12, 345);

----------------
-- # box.stat
----------------;
for k, v in pairs(box.stat()) do
    print(' - ', k)
end;
for k, v in pairs(box.stat().DELETE) do
    print(' - ', k)
end;
for k, v in pairs(box.stat.DELETE) do
    print(' - ', k)
end;

----------------
-- # box.space
----------------;
type(box);
type(box.space);
box.cfg.memcached_space;
for i, v in pairs(box.space[0].index[0].key_field[0]) do
    print(i, ' : ', v)
end;

----------------
-- # box.space
----------------;
string.match(tostring(box.slab), '^table:') ~= nil;
box.slab.arena_used >= 0;
box.slab.arena_size > 0;
string.match(tostring(box.slab.slabs), '^table:') ~= nil;
for k, v in pairs(box.slab()) do
    print(' - ', k)
end;

----------------
-- # box.error
----------------;
for k,v in pairs(box.error) do
    print('box.error.', k, ' : ', v)
end;
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
