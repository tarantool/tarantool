local t = require("luatest")
local g = t.group()
local server = require("luatest.server")

-- Create test instance.
g.before_all = function()
    g.server = server:new({alias = 'test-gh-2867'})
    g.server:start()
end

-- Stop test instance.
g.after_all = function()
    g.server:stop()
    g.server:drop()
end

g.test_box_cfg_set_known_option = function()
    t.assert_equals(g.server:eval("return box.cfg.read_only"), false)
    local ok, err = g.server:eval([[
        return pcall(function() box.cfg.read_only=true end)]])
    t.assert_equals(ok, false)
    local err_msg = "Use box.cfg{read_only = true} for update"
    t.assert_str_contains(tostring(err), err_msg)
    t.assert_equals(g.server:eval("return box.cfg.read_only"), false)
end

g.test_box_cfg_set_unknown_option = function()
    t.assert_equals(g.server:eval("return box.cfg.LANGO_TEAM"), nil)
    local ok, err = g.server:eval([[
        return pcall(function() box.cfg.LANGO_TEAM=true end)]])
    t.assert_equals(ok, false)
    local err_msg = "Attempt to modify a read-only table"
    t.assert_str_contains(tostring(err), err_msg)
    t.assert_equals(g.server:eval("return box.cfg.LANGO_TEAM"), nil)
end
