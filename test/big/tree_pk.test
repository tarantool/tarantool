dofile('utils.lua');

-- integer keys;
box.space[2]:insert(1, 'tuple');
save snapshot;
box.space[2]:insert(2, 'tuple 2');
save snapshot;

box.space[2]:insert(3, 'tuple 3');
box.space[2]:select(0, 1);
box.space[2]:select(0, 2);
box.space[2]:select(0, 3);

-- Cleanup;
box.space[2]:delete(1);
box.space[2]:delete(2);
box.space[2]:delete(3);

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624;
box.space[2]:insert('xxxxxxx');
box.space[2]:insert('');
box.space[2]:insert('12');

-- string keys;
box.space[3]:insert('identifier', 'tuple');
save snapshot;
box.space[3]:insert('second', 'tuple 2');
save snapshot;
box.select_range('3', '0', '100', 'second');
box.select_range('3', '0', '100', 'identifier');

box.space[3]:insert('third', 'tuple 3');
box.space[3]:select(0, 'identifier');
box.space[3]:select(0, 'second');
box.space[3]:select(0, 'third');

-- Cleanup;
box.space[3]:delete('identifier');
box.space[3]:delete('second');
box.space[3]:delete('third');

function box.crossjoin(space0, space1, limit)
    space0 = tonumber(space0)
    space1 = tonumber(space1)
    limit = tonumber(limit)
    local result = {}
    for k0, v0 in box.space[space0]:pairs() do
        for k1, v1 in box.space[space1]:pairs() do
            if limit <= 0 then
                return unpack(result)
            end
            newtuple = {v0:unpack()}
            for _, v in v1:pairs() do
                table.insert(newtuple, v)
            end
            table.insert(result, box.tuple.new(newtuple))
            limit = limit - 1
        end
    end
    return unpack(result)
end;

box.space[2]:insert(1, 'tuple');
box.space[3]:insert(1, 'tuple');
box.space[3]:insert(2, 'tuple');

box.crossjoin('3', '3', '0');
box.crossjoin('3', '3', '5');
box.crossjoin('3', '3', '10000');
box.crossjoin('3', '2', '10000');
box.space[3]:truncate();

-- Bug #922520 - select missing keys;
box.space[2]:insert(200, 'select me!');
box.space[2]:select(0, 200);
box.space[2]:select(0, 199);
box.space[2]:select(0, 201);

-- Test partially specified keys in TREE indexes;
box.space[15]:insert('abcd');
box.space[15]:insert('abcda');
box.space[15]:insert('abcda_');
box.space[15]:insert('abcdb');
box.space[15]:insert('abcdb_');
box.space[15]:insert('abcdb__');
box.space[15]:insert('abcdb___');
box.space[15]:insert('abcdc');
box.space[15]:insert('abcdc_');
unpack(box.sort({box.space[15].index[0]:select_range(3, 'abcdb')}));
box.space[15]:truncate();

--
-- tree::replace tests
--;

box.space[22]:truncate();

box.space[22]:insert(0, 0, 0, 0);
box.space[22]:insert(1, 1, 1, 1);
box.space[22]:insert(2, 2, 2, 2);

-- OK;
box.replace_if_exists(22, 1, 1, 1, 1);
box.replace_if_exists(22, 1, 10, 10, 10);
box.replace_if_exists(22, 1, 1, 1, 1);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);
box.space[22]:select(0, 1);
box.space[22]:select(1, 1);
box.space[22]:select(2, 1);
box.space[22]:select(3, 1);

-- OK;
box.space[22]:insert(10, 10, 10, 10);
box.space[22]:delete(10);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);


-- TupleFound (primary key);
box.space[22]:insert(1, 10, 10, 10);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);
box.space[22]:select(0, 1);

-- TupleNotFound (primary key);
box.replace_if_exists(22, 10, 10, 10, 10);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);

-- TupleFound (key #1);
box.space[22]:insert(10, 0, 10, 10);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);
box.space[22]:select(1, 0);

-- TupleFound (key #1);
box.replace_if_exists(22, 2, 0, 10, 10);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);
box.space[22]:select(1, 0);

-- TupleFound (key #3);
box.space[22]:insert(10, 10, 10, 0);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);
box.space[22]:select(3, 0);

-- TupleFound (key #3);
box.replace_if_exists(22, 2, 10, 10, 0);
box.space[22]:select(0, 10);
box.space[22]:select(1, 10);
box.space[22]:select(2, 10);
box.space[22]:select(3, 10);
box.space[22]:select(3, 0);

-- Non-Uniq test (key #2);
box.space[22]:insert(4, 4, 0, 4);
box.space[22]:insert(5, 5, 0, 5);
box.space[22]:insert(6, 6, 0, 6);
box.replace_if_exists(22, 5, 5, 0, 5);
unpack(box.sort({box.space[22]:select(2, 0)}));
box.space[22]:delete(5);
unpack(box.sort({box.space[22]:select(2, 0)}));

box.space[22]:truncate();

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
