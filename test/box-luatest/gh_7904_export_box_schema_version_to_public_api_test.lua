local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that a deprecation warning is printed, exactly once, when using `box.internal.schema_version`.
g.test_box_internal_schema_version_deprecation = function(cg)
    cg.server:exec(function()
        box.internal.schema_version()
    end)
    local deprecation_warning =
        'box.internal.schema_version will be removed, please use box.info.schema_version instead'
    t.assert_is_not(cg.server:grep_log(deprecation_warning, 1024), nil)
    fio.truncate(g.server.log_file)
    cg.server:exec(function()
        box.internal.schema_version()
    end)
    t.assert_is(cg.server:grep_log(deprecation_warning, 1024), nil)
end
