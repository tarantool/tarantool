local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'default',
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_different_logs_on_new_and_free = function()
    g.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.cfg{log_level = 7}
        s:replace{0, 0, 0, 0, 0, 0, 0}
        box.cfg{log_level = 5}
    end)

    local str = g.server:grep_log('memtx_tuple_new[%w_]*%(%d+%) = 0x%x+$', 1024)
    local new_tuple_address = string.match(str, '0x%x+$')
    new_tuple_address = string.sub(new_tuple_address, 3)

    g.server:exec(function()
        box.cfg{log_level = 7}
        box.space.test:replace{0, 1}
        collectgarbage('collect')
        box.cfg{log_level = 5}
    end)

    str = g.server:grep_log('memtx_tuple_delete%w*%(0x%x+%)', 1024)
    local deleted_tuple_address = string.match(str, '0x%x+%)')
    deleted_tuple_address = string.sub(deleted_tuple_address, 3, -2)
    t.assert_equals(new_tuple_address, deleted_tuple_address)
end
