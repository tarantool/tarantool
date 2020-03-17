utils = dofile('utils.lua')

hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'unsigned', 2, 'string', 3, 'unsigned'}, unique = true })
tmp = hash:create_index('unique', { type = 'hash', parts = {3, 'unsigned', 5, 'unsigned'}, unique = true })

-- insert rows
hash:insert{0, 'foo', 0, '', 1}
hash:insert{0, 'foo', 1, '', 1}
hash:insert{1, 'foo', 0, '', 2}
hash:insert{1, 'foo', 1, '', 2}
hash:insert{0, 'bar', 0, '', 3}
hash:insert{0, 'bar', 1, '', 3}
hash:insert{1, 'bar', 0, '', 4}
hash:insert{1, 'bar', 1, '', 4}

-- try to insert a row with a duplicate key
hash:insert{1, 'bar', 1, '', 5}

-- output all rows
env = require('test_run')
test_run = env.new()
test_run:cmd("setopt delimiter ';'")
function select_all()
    local result = {}
    local tuple, v
    for tuple, v in hash:pairs() do
        table.insert(result, v)
    end
    return result
end;
test_run:cmd("setopt delimiter ''");
utils.sort(select_all())
select_all = nil

-- primary index select
hash.index['primary']:get{1, 'foo', 0}
hash.index['primary']:get{1, 'bar', 0}
-- primary index select with missing part
hash.index['primary']:get{1, 'foo'}
-- primary index select with extra part
hash.index['primary']:get{1, 'foo', 0, 0}
-- primary index select with wrong type
hash.index['primary']:get{1, 'foo', 'baz'}

-- secondary index select
hash.index['unique']:get{1, 4}
-- secondary index select with no such key
hash.index['unique']:get{1, 5}
-- secondary index select with missing part
hash.index['unique']:get{1}
-- secondary index select with wrong type
hash.index['unique']:select{1, 'baz'}

-- cleanup
hash:drop()
