local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {vinyl_cache = 0},
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_deferred_delete_generated_on_commit = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local function join(f)
            local ok, err = f:join(5)
            if not ok then
                error(err)
            end
        end

        local s = box.schema.space.create('test', {
            engine = 'vinyl',
            defer_deletes = true,
        })
        s:create_index('primary')
        s:create_index('secondary', {parts = {2, 'unsigned'}})
        s:insert({1, 1})
        s:insert({2, 2})

        local ch1 = fiber.channel(1)
        local ch2 = fiber.channel(1)

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        -- Since INSERT{2, 2} is stored in the memory layer, DELETE{2} must be
        -- generated for the secondary index when the transaction is prepared.
        local f1 = fiber.new(box.atomic, function()
            s:delete({2})
            ch1:put(true)
        end)
        f1:set_joinable(true)
        t.assert(ch1:get(5))

        local f2 = fiber.new(box.atomic, function()
            -- This statement must not fail because the transaction operates in
            -- the read-committed isolation level and DELETE{2} was prepared
            -- for commit.
            s:replace{1, 2}
            ch1:put(true)
            ch2:get(5)
        end)
        f2:set_joinable(true)
        t.assert(ch1:get(5))

        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert_error_covers({name = 'WAL_IO'}, join, f1)

        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        -- The transaction must be aborted because DELETE{2} overwriting
        -- INSERT{2, 2} was rolled back.
        ch2:put(true)
        t.assert_error_covers({name = 'TRANSACTION_CONFLICT'}, join, f2)

        local expected = {{1, 1}, {2, 2}}
        t.assert_equals(s.index.primary:select(), expected)
        t.assert_equals(s.index.secondary:select(), expected)
    end)
end

g.test_deferred_delete_postponed_to_compaction = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local function join(f)
            local ok, err = f:join(5)
            if not ok then
                error(err)
            end
        end

        local s = box.schema.space.create('test', {
            engine = 'vinyl',
            defer_deletes = true,
        })
        s:create_index('primary')
        s:create_index('secondary', {parts = {2, 'unsigned'}})
        s:insert({1, 1})
        s:insert({2, 2})
        box.snapshot()

        local ch1 = fiber.channel(1)
        local ch2 = fiber.channel(1)

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        -- Since INSERT{2, 2} is stored on disk, DELETE{2} will not be
        -- generated for the secondary index when the transaction is prepared.
        local f1 = fiber.new(box.atomic, function()
            s:delete({2})
            ch1:put(true)
        end)
        f1:set_joinable(true)
        t.assert(ch1:get(5))

        local f2 = fiber.new(box.atomic, function()
            -- This statement must not fail because the transaction operates in
            -- the read-committed isolation level and DELETE{2} was prepared
            -- for commit.
            s:replace({1, 2})
            ch1:put(true)
            ch2:get(5)
        end)
        f2:set_joinable(true)
        t.assert(ch1:get(5))

        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert_error_covers({name = 'WAL_IO'}, join, f1)

        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        -- The transaction must be aborted because DELETE{2} overwriting
        -- INSERT{2, 2} was rolled back.
        ch2:put(true)
        t.assert_error_covers({name = 'TRANSACTION_CONFLICT'}, join, f2)

        local expected = {{1, 1}, {2, 2}}
        t.assert_equals(s.index.primary:select(), expected)
        t.assert_equals(s.index.secondary:select(), expected)
    end)
end
