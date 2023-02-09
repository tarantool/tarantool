local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_old_behavior_init = function()
    local s = server:new({
        alias = 'old_behavior',
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                require('compat').sql_seq_scan_default = 'old'
            ]]
        }
    })
    s:start()
    s:exec(function()
        local res = box.execute([[SELECT * FROM "_vspace";]])
        t.assert(res.rows ~= nil)
    end)
    s:stop()
end

g.test_new_behavior_init = function()
    local s = server:new({
        alias = 'new_behavior',
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                require('compat').sql_seq_scan_default = 'new'
            ]]
        }
    })
    s:start()
    s:exec(function()
        local _, err = box.execute([[SELECT * FROM "_vspace";]])
        t.assert_equals(err.message, "Scanning is not allowed for '_vspace'")
    end)
    s:stop()
end

g.test_new_sessions = function()
    g.server:exec(function()
        local func = function()
            return box.execute([[select * from "_vspace";]])
        end
        local fiber = require('fiber')

        require('compat').sql_seq_scan_default = 'new'
        local f = fiber.new(func)
        f:set_joinable(true)
        local _, res, err = f:join()
        t.assert(res == nil)
        t.assert_equals(err.message, "Scanning is not allowed for '_vspace'")

        require('compat').sql_seq_scan_default = 'old'
        f = fiber.new(func)
        f:set_joinable(true)
        _, res, err = f:join()
        t.assert(res ~= nil)
        t.assert(err == nil)
    end)
end
