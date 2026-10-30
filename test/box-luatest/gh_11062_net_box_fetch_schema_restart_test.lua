local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{
        -- This should make the fetch schema requests issued by a net.box
        -- client get throttled, thus opening a time window for the server
        -- to update the schema.
        box_cfg = {net_msg_max = 2},
    }
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_fetch_schema_restart = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        fiber.create(function()
            for i = 1, 10000 do
                local s = box.schema.space.create('test' .. i)
                s:create_index('pk')
            end
        end)
    end)
    local conn = net.connect(cg.server.net_box_uri, {fetch_schema = true})
    for _ = 1, 10000 do
        t.assert(conn:ping())
    end
    -- Sic: do not close the connection to check that the server does not hang
    -- on graceful shutdown.
end
