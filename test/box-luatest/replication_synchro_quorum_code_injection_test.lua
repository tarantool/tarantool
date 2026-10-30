local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'server'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_code_injection = function()
    local res = g.server:exec(function()
        rawset(_G, 'res', false)
        pcall(box.cfg, {replication_synchro_quorum =
            [=[N / 2 + 1]] rawset(_G, 'res', true) --[[]=]})
        return rawget(_G, 'res')
    end)
    t.assert_is(res, false)
end
