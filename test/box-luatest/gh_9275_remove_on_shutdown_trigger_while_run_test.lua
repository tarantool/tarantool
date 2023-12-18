local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.test_remove_on_shutdown_trigger_while_run = function()
    local server = server:new()
    server:start()
    server:exec(function()
        local fiber = require('fiber')

        -- Yield at the end of each handler so that underlyig
        -- trigger iterator makes a step and unreferences the trigger.
        local function h1()
            box.ctl.on_shutdown(nil, h1)
            fiber.sleep(0)
        end
        local function h2()
            box.ctl.on_shutdown(nil, nil, 'h3')
            fiber.sleep(0)
        end
        local function h3()
            box.ctl.on_shutdown(nil, nil, 'h2')
            fiber.sleep(0)
        end
        box.ctl.on_shutdown(h1)
        box.ctl.on_shutdown(h2, nil, 'h2')
        box.ctl.on_shutdown(h3, nil, 'h3')
    end)
    server:drop()
end
