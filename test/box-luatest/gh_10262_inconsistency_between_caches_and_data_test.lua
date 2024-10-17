local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_reproducer_from_issue = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local space = box.schema.space.create('test')
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_BUILD_INDEX_TIMEOUT', 0.01)
        local index
        fiber.create(function()
            box.begin()
            index = space:create_index('pk')
            for i = 1, 100 do
                space:replace({i, 2})
            end
            box.commit()
        end)
        local function alter_index()
            index:alter({parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        end
        t.assert_error_msg_content_equals(
            "Can't modify space '512': the space was concurrently modified",
            alter_index)
    end)
end

g.test_space_cache_consistent_with_data = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local space = box.schema.space.create('test')
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(function()
            space:create_index('pk')
        end)
        t.assert_not_equals(space.index[0], nil)
        t.assert_not_equals(box.space._index:get{space.id, 0}, nil)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end

g.test_func_cache_consistent_with_data = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(function()
            box.schema.func.create('test', {
                language = 'Lua', body = 'function() return 42 end'})
        end)
        t.assert_not_equals(box.func.test, nil)
        t.assert_not_equals(box.space._func.index[2]:get('test'), nil)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end
