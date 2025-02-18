local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            vinyl_cache = 64 * 1024 * 1024,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_deferred_delete_rollback = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = true,
        })
        s:create_index('primary')
        s:create_index('secondary', {unique = false, parts = {2, 'unsigned'}})

        s:insert{1, 10}
        s:insert{2, 20}
        s:insert{3, 30}
        s:insert{4, 40}
        s:insert{5, 50}
        box.snapshot()

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        local f = fiber.new(function()
            box.begin()
            s:delete({1})
            s:delete({3})
            s:delete({5})
            box.commit()
        end)
        f:set_joinable(true)
        fiber.yield()

        box.begin{txn_isolation = 'read-committed'}
        t.assert_equals(s.index.secondary:select(), {{2, 20}, {4, 40}})
        box.commit()

        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert_error_covers({
            type = 'ClientError',
            name = 'WAL_IO',
        }, function()
            local ok, err = f:join(5)
            if not ok then
                error(err)
            end
        end)

        box.begin{txn_isolation = 'read-committed'}
        t.assert_equals(s.index.secondary:select(), {
            {1, 10}, {2, 20}, {3, 30}, {4, 40}, {5, 50},
        })
        box.commit()
    end)
end

g.after_test('test_deferred_delete_rollback', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)
