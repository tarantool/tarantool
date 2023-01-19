local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_read_view = function(cg)
    t.tarantool.skip_if_enterprise()
    cg.server:exec(function()
        local t = require('luatest')
        t.assert_error_msg_equals(
            "Community edition does not support read view",
            function() box.read_view.open() end)
    end)
end
