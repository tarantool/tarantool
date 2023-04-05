local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{type = 'xlog'}, {type = 'snap'}})

g.before_each(function(cg)
    cg.server = server:new({
        alias = 'gh_7917_' .. cg.params.type,
        datadir = fio.pathjoin('test/box-luatest/gh_7917_data',
                               cg.params.type),
    })
    cg.server:start({wait_until_ready = false})
    cg.log_filename = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log')
end)

g.test_log_row_on_recovery_error = function(cg)
    t.helpers.retrying({}, function()
        t.assert_str_matches(
            cg.server:grep_log('error at request: .*', nil,
                               {filename = cg.log_filename}),
            '.*{"name": "a", "type": "st"}.*')
    end)
end

g.after_each(function(cg)
    cg.server:drop()
end)
