local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_tx_delete = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')
        for i = 10, 50, 10 do
            s:insert({i})
        end

        local c1 = fiber.channel(1)
        local c2 = fiber.channel(1)
        local f = fiber.new(function()
            box.begin()
            s:delete({10})
            s:delete({30})
            s:delete({50})
            s:select()
            s:select({}, {iterator = 'lt'})
            c1:put(true)
            c2:get()
            box.commit()
        end)
        f:set_joinable(true)
        t.assert_equals(c1:get(5), true)

        -- The DELETE statements must be invisible because they haven't been
        -- committed yet.
        t.assert_equals(s:select(), {{10}, {20}, {30}, {40}, {50}})
        t.assert_equals(s:select({}, {iterator = 'lt'}),
                        {{50}, {40}, {30}, {20}, {10}})

        c2:put(true)
        t.assert_equals({f:join(5)}, {true})

        t.assert_equals(s:select(), {{20}, {40}})
        t.assert_equals(s:select({}, {iterator = 'lt'}), {{40}, {20}})
    end)
end

g.test_prepared_delete = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')
        for i = 10, 50, 10 do
            s:insert({i})
        end

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        local c = fiber.channel(1)
        local f = fiber.new(function()
            box.begin()
            s:delete({10})
            s:delete({30})
            s:delete({50})
            c:put(true)
            box.commit()
        end)
        f:set_joinable(true)
        t.assert_equals(c:get(5), true)

        -- The DELETE statements must be visible with the 'read-committed'
        -- isolation level, but not with 'read-confirmed' because they haven't
        -- been written to the WAL yet.
        box.begin{txn_isolation = 'read-committed'}
        t.assert_equals(s:select(), {{20}, {40}})
        t.assert_equals(s:select({}, {iterator = 'lt'}), {{40}, {20}})
        box.commit()

        box.begin{txn_isolation = 'read-confirmed'}
        t.assert_equals(s:select(), {{10}, {20}, {30}, {40}, {50}})
        t.assert_equals(s:select({}, {iterator = 'lt'}),
                        {{50}, {40}, {30}, {20}, {10}})
        box.commit()

        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert_equals({f:join(5)}, {true})
    end)
end

g.after_test('test_prepared_delete', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)

g.test_confirmed_delete = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')
        for i = 10, 50, 10 do
            s:insert({i})
        end

        box.begin()
        t.assert_equals(s:get(30), {30})

        local f = fiber.new(function()
            box.begin()
            s:delete({10})
            s:delete({30})
            s:delete({50})
            box.commit()
            s:select()
            s:select({}, {iterator = 'lt'})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join(5)}, {true})

        -- The transaction must be able to see the deleted tuples because
        -- it was sent to a read view on deletion.
        t.assert_equals(s:select(), {{10}, {20}, {30}, {40}, {50}})
        t.assert_equals(s:select({}, {iterator = 'lt'}),
                        {{50}, {40}, {30}, {20}, {10}})
        box.commit()

        t.assert_equals(s:select(), {{20}, {40}})
        t.assert_equals(s:select({}, {iterator = 'lt'}), {{40}, {20}})
    end)
end

g.test_deferred_delete = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = true,
        })
        s:create_index('primary')
        s:create_index('secondary', {unique = false, parts = {2, 'unsigned'}})
        for i = 10, 70, 10 do
            s:insert({i, i * 10})
        end
        box.snapshot()

        box.begin()
        t.assert_equals(s:get(30), {30, 300})

        local f = fiber.new(function()
            box.begin()
            s:delete({10})
            s:delete({30})
            s:delete({50})
            s:delete({70})
            box.commit()
            s.index.secondary:select()
            s.index.secondary:select({}, {iterator = 'lt'})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join(5)}, {true})

        -- The transaction must be able to see the deleted tuples because
        -- it was sent to a read view on deletion.
        t.assert_equals(s.index.secondary:select(), {
            {10, 100}, {20, 200}, {30, 300}, {40, 400}, {50, 500},
            {60, 600}, {70, 700},
        })
        t.assert_equals(s.index.secondary:select({}, {iterator = 'lt'}), {
            {70, 700}, {60, 600}, {50, 500}, {40, 400}, {30, 300},
            {20, 200}, {10, 100},
        })
        box.commit()

        t.assert_equals(s.index.secondary:select(),
                        {{20, 200}, {40, 400}, {60, 600}})
        t.assert_equals(s.index.secondary:select({}, {iterator = 'lt'}),
                        {{60, 600}, {40, 400}, {20, 200}})
    end)
end

g.test_partial_key_forward = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        s:replace({0, 2})
        s:replace({2, 0})

        box.begin()
        t.assert_equals(s:select(), {{0, 2}, {2, 0}})

        local f = fiber.new(function()
            s:replace({0, 1})
            s:delete({0, 2})
            t.assert_equals(s:select(), {{0, 1}, {2, 0}})
            t.assert_equals(s:select({1}, {iterator = 'gt'}), {{2, 0}})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join(5)}, {true})

        t.assert_equals(s:select({}, {iterator = 'lt'}), {{2, 0}, {0, 2}})
        t.assert_equals(s:select({0, 1}, {iterator = 'gt'}), {{0, 2}, {2, 0}})
        box.commit()
    end)
end

g.test_partial_key_backward = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        s:replace({0, 2})
        s:replace({2, 0})

        box.begin()
        t.assert_equals(s:select(), {{0, 2}, {2, 0}})

        local f = fiber.new(function()
            s:replace({2, 1})
            s:delete({2, 0})
            t.assert_equals(s:select(), {{0, 2}, {2, 1}})
            t.assert_equals(s:select({1}, {iterator = 'lt'}), {{0, 2}})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join(5)}, {true})

        t.assert_equals(s:select({}, {iterator = 'gt'}), {{0, 2}, {2, 0}})
        t.assert_equals(s:select({2, 1}, {iterator = 'lt'}), {{2, 0}, {0, 2}})
        box.commit()
    end)
end
