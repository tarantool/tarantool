local server = require('luatest.server')
local t = require('luatest')
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
        if box.space.memtx ~= nil then
            box.space.memtx:drop()
        end
        if box.space.vinyl ~= nil then
            box.space.vinyl:drop()
        end
    end)
end)

-- Test the feature does not work in CE engines.
g.test_delete_range = function(cg)
    cg.server:exec(function()
        local space_memtx = box.schema.create_space('memtx', {engine = 'memtx'})
        local space_vinyl = box.schema.create_space('vinyl', {engine = 'vinyl'})

        space_memtx:create_index('pk')
        space_vinyl:create_index('pk')

        t.assert_error_msg_equals('memtx does not support range deletion',
                                  space_memtx.delete_range, space_memtx, {}, {})
        t.assert_error_msg_equals('memtx does not support range deletion',
                                  space_memtx.index.pk.delete_range,
                                  space_memtx.index.pk, {}, {})
        t.assert_error_msg_equals('vinyl does not support range deletion',
                                  space_vinyl.delete_range, space_vinyl, {}, {})
        t.assert_error_msg_equals('vinyl does not support range deletion',
                                  space_vinyl.index.pk.delete_range,
                                  space_vinyl.index.pk, {}, {})
    end)
end
