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
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_tx_abort_on_timeout = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local timeout = 0.1

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:replace({1})
        box.snapshot()

        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', true)
        fiber.new(function()
            fiber.sleep(timeout * 2)
            box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
        end)

        box.begin({timeout = timeout})
        s:update({1}, {{'!', 2, 20}})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'TRANSACTION_TIMEOUT',
        }, box.commit)
        t.assert_equals(s:select(), {{1}})
    end)
end
