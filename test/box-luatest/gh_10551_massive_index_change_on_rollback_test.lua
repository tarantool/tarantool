local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_test('test_massive_index_change_on_rollback', function(cg)
    cg.server:exec(function()
        for i = 1,10 do
            local s = box.schema.create_space('test' .. i)
            s:create_index('pk')
            s:insert({1})
        end
    end)
end)

g.after_test('test_massive_index_change_on_rollback', function(cg)
    cg.server:exec(function()
        for i = 1,10 do
            local s = box.space['test' .. i]
            if s ~= nil then
                s:drop()
            end
        end
    end)
end)

g.test_massive_index_change_on_rollback = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local errinj = box.error.injection
        local fibers = {}
        errinj.set('ERRINJ_WAL_DELAY', true)
        for i = 1,10 do
            local f = fiber.new(function()
                local s = box.space['test' .. i]
                s:insert({2})
            end)
            fibers[i] = f
            f:set_joinable(true)
            f:wakeup()
            fiber.yield()
        end
        fiber.create(function()
            box.snapshot()
        end)
        errinj.set('ERRINJ_INDEX_ALLOC', true)
        errinj.set('ERRINJ_WAL_WRITE_DISK', true)
        errinj.set('ERRINJ_WAL_DELAY', false)
        -- Test rollback is correct.
        for i = 1,10 do
            fibers[i]:join()
            local s = box.space['test' .. i]
            t.assert_equals(s:count(), 1)
        end
        errinj.set('ERRINJ_INDEX_ALLOC', false)
        errinj.set('ERRINJ_WAL_WRITE_DISK', false)
        -- Now index memory is allocated using malloc. Do insertions
        -- to allocate index blocks as usual and thus mix malloc and default
        -- blocks. Check index works in this case.
        for i = 1,10 do
            local s = box.space['test' .. i]
            for j = 2, 1000 do
                s:insert({j})
            end
            for j = 1, 1000 do
                s:delete({j})
            end
        end
    end)
end
