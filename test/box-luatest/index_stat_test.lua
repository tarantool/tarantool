local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Check the stat() method error messages.
g.test_index_stat_errors = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local pk = s:create_index('pk')
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "Use index:stat(...) instead of index.stat(...)",
        }, pk.stat)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "Usage: box.internal.stat(space_id, index_id)",
        }, box.internal.stat, 'abc', pk.id)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "Usage: box.internal.stat(space_id, index_id)",
        }, box.internal.stat, s.id, 'abc')
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_SPACE',
            message = "Space '100500' does not exist",
        }, box.internal.stat, 100500, pk.id)
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_INDEX_ID',
            message = "No index #100500 is defined in space 'test'",
        }, box.internal.stat, s.id, 100500)
    end)
end

-- Test the stat() method of memtx indexes.
g.test_index_stat_memtx = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('i1', {type = 'tree'})
        s:create_index('i2', {type = 'hash'})
        s:create_index('i3', {type = 'bitset', parts = {3, 'unsigned'}})
        s:create_index('i4', {type = 'rtree', parts = {4, 'array'}})
        t.assert_equals(s.index.i1:stat(), {})
        t.assert_equals(s.index.i2:stat(), {})
        t.assert_equals(s.index.i3:stat(), {})
        t.assert_equals(s.index.i4:stat(), {})
    end)
end
