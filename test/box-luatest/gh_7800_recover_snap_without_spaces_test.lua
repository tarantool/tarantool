local t = require('luatest')

local g = t.group('gh-7800')

g.before_all(function()
    local server = require('luatest.server')
    g.server = server:new({alias = 'master',
                           datadir = 'test/box-luatest/gh_7800_data'})
    g.server:start({wait_until_ready = false})
end)

g.after_all(function()
    g.server:drop()
end)

g.test_recovery = function()
    t.helpers.retrying({}, function()
        local fio = require('fio')
        local msg = "has no system spaces"
        local filename = fio.pathjoin(g.server.workdir,
                                      g.server.alias .. '.log')
        t.assert(g.server:grep_log(msg, nil, {filename = filename}))
    end)
end
