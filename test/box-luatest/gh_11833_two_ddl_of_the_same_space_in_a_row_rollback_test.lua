local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-11833-two-ddl-of-the-same-space-in-a-row-rollback')
--
-- gh-11833: two ddl of the same space in a row rollback
--

g.before_all(function()
    t.tarantool.skip_if_not_debug()

    g.server = server:new{}
    g.server:start()

    g.server:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('pk')
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)

g.test_two_ddl_of_the_same_space_in_a_row_rollback = function()
    g.server:exec(function()
        local fiber = require('fiber')

        local format_default = {
            {'a', 'unsigned'},
            {'b', 'unsigned'},
        }

        local format_nullable = {
            {'a', 'unsigned'},
            {'b', 'unsigned', is_nullable = true},
        }

        box.space.test:format(format_default)

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        -- Change format in _space and wait for it to go to WAL.
        fiber.create(function()
            box.space.test:format(format_nullable)
        end)

        -- Change format in _space and wait for the first fiber to complete.
        local f = fiber.create(function()
            box.space.test:format(format_default)
        end)
        f:set_joinable(true)

        -- WAL write failed so rollback in _space is done for the format
        -- change from the first fiber which breaks change order.
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        local ok, err = f:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.ALTER_SPACE,
            message = string.format("Can't modify space '%d': " ..
                "the space was concurrently modified", box.space.test.id)
        })
    end)
end

g.test_third_ddl_while_second_waits_in_alter_yield_guard = function()
    g.server:exec(function()
        local fiber = require('fiber')

        local format_default = {
            {'a', 'unsigned'},
            {'b', 'unsigned'},
        }

        local format_nullable = {
            {'a', 'unsigned'},
            {'b', 'unsigned', is_nullable = true},
        }

        box.space.test:format(format_default)

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        local f1 = fiber.create(function()
            box.space.test:format(format_nullable)
        end)
        f1:set_joinable(true)

        local f2 = fiber.create(function()
            box.space.test:format(format_default)
        end)
        f2:set_joinable(true)

        local f3 = fiber.new(function()
            box.space.test:format(format_default)
        end)
        f3:set_joinable(true)

        local ok, err = f3:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.ALTER_SPACE,
            message = string.format("Can't modify space 'test': " ..
                "the space is already being modified")
        })

        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert(f1:join())
        t.assert(f2:join())
    end)
end
