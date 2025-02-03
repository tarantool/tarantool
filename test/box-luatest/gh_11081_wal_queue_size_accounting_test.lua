local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    t.tarantool.skip_if_not_debug()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

-- Deliberately avoid dropping test space. It would hang.

g.test_accounting_on_error = function(g)
    g.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.cfg{wal_queue_max_size = 100}
        box.error.injection.set('ERRINJ_WAL_IO', true)
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WAL_IO,
            message = 'Failed to write to disk',
        }, s.insert, s, {1, string.rep('a', 1000)})
        box.error.injection.set('ERRINJ_WAL_IO', false)
        local f = fiber.new(function()
            s:insert({1})
        end)
        f:set_joinable(true)
        t.assert(f:join(10))
    end)
end
