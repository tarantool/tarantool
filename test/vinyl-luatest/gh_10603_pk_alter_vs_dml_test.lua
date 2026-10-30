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

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_empty_pk_alter = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local timeout = 10 -- seconds

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')

        local c1 = fiber.channel(1)
        local f1 = fiber.new(function()
            c1:get()
            s:replace({1, 10})
        end)
        f1:set_joinable(true)

        local c2 = fiber.channel(1)
        local f2 = fiber.new(function()
            c2:get()
            s.index.pk:alter({parts = {2, 'unsigned'}})
        end)
        f2:set_joinable(true)

        local c3 = fiber.channel(1)
        local f3 = fiber.new(function()
            c3:get()
            s:replace({2, 20})
        end)
        f3:set_joinable(true)

        -- Fiber 1 (DML) inserts a tuple into the space and blocks on WAL.
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        c1:put(true)
        fiber.yield()

        -- Fiber 2 (DDL) waits for pending WAL writes to complete.
        c2:put(true)
        fiber.yield()

        -- Fiber 3 (DML) inserts a tuple into the space and blocks on WAL.
        c3:put(true)
        fiber.yield()

        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        fiber.yield()

        -- Fiber 1 (DML) completes successfully.
        t.assert_equals({f1:join(timeout)}, {true})

        -- Fiber 2 (DDL) fails because the primary index isn't empty.
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.UNUSUPPORTED,
            message = 'Vinyl does not support ' ..
                      'rebuilding the primary index of a non-empty space',
        }, function()
            local ok, err = f2:join(timeout)
            if not ok then
                error(err)
            end
        end)

        -- Fiber 3 (DML) completes successfully.
        t.assert_equals({f3:join(timeout)}, {true})

        t.assert_equals(box.space.test:select({}, {fullscan = true}),
                        {{1, 10}, {2, 20}})
    end)
end
