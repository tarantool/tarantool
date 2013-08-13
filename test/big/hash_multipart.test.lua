-- setopt delim ';'
dofile('utils.lua')

-- insert rows;
box.space[27]:insert(0, 'foo', 0, '', 1);
box.space[27]:insert(0, 'foo', 1, '', 1);
box.space[27]:insert(1, 'foo', 0, '', 2);
box.space[27]:insert(1, 'foo', 1, '', 2);
box.space[27]:insert(0, 'bar', 0, '', 3);
box.space[27]:insert(0, 'bar', 1, '', 3);
box.space[27]:insert(1, 'bar', 0, '', 4);
box.space[27]:insert(1, 'bar', 1, '', 4);

-- try to insert a row with a duplicate key;
box.space[27]:insert(1, 'bar', 1, '', 5);

-- output all rows;
function box.select_all(space)
    local result = {}
    for k, v in box.space[space]:pairs() do
        table.insert(result, v)
    end
    return result
end;

unpack(box.sort(box.select_all(27)))

-- primary index select;
box.space[27]:select(0, 1, 'foo', 0);
box.space[27]:select(0, 1, 'bar', 0);
-- primary index select with missing part;
box.space[27]:select(0, 1, 'foo');
-- primary index select with extra part;
box.space[27]:select(0, 1, 'foo', 0, 0);
-- primary index select with wrong type;
box.space[27]:select(0, 1, 'foo', 'baz');

-- secondary index select;
box.space[27]:select(1, 1, 4);
-- secondary index select with no such key;
box.space[27]:select(1, 1, 5);
-- secondary index select with missing part;
box.space[27]:select(1, 1);
-- secondary index select with wrong type;
box.space[27]:select(1, 1, 'baz');

-- cleanup;
box.space[27]:truncate();
box.space[27]:len();

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
