local t = require('luatest')
local g = t.group('gh-8287')

g.before_all(function(cg)
    -- Set initial log level as a string
    local box_cfg = {log_level = 'verbose'}
    local server = require('luatest.server')
    cg.server = server:new({alias = 'gh_8287', box_cfg = box_cfg})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that log level printed to the log during first box.cfg{} is correct
g.test_log_level = function(cg)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log("I> log level 6") ~= nil)
    end)
end
