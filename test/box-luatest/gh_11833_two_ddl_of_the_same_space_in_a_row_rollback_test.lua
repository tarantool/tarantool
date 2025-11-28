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
        box.schema.create_space('test', {format = {
            {'a', 'unsigned'},
            {'b', 'unsigned'},
        }})
        box.space.test:create_index('pk')
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_two_ddl_of_the_same_space_in_a_row_rollback = function()
    g.server:exec(function()
        local fiber = require('fiber')

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        -- Change format in _space and wait it to be written to WAL.
        fiber.create(function()
            box.space.test:format({
                {'a', 'unsigned'},
                {'b', 'unsigned', is_nullable = true},
            })
        end)

        -- Change format in _space and wait for the first change to complete.
        local f = fiber.create(function()
            box.space.test:format({
                {'a', 'unsigned'},
                {'b', 'unsigned'},
            })
        end)
        f:set_joinable(true)

        -- WAL write failed so rollback in _space is done for the format
        -- change from the first fiber which breaks change order.
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        local _, err = f:join()
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.CASCADE_ROLLBACK,
            message = string.format("WAL has a rollback in progress",
                box.space.test.id)
        })
    end)
end
