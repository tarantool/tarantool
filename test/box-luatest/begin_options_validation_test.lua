local server = require("luatest.server")
local t = require("luatest")

local g = t.group("begin-options-validation")

g.before_all(function()
    g.server = server:new{
        alias = "default",
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_begin_options_validation = function()
    g.server:exec(function()
        t.assert_error_msg_content_equals(
            "unexpected option 'foo'",
            box.begin, {foo = "bar"})
        t.assert_error_msg_content_equals(
            "timeout must be a number greater than 0",
            box.begin, {timeout = "not-a-number"})
        t.assert_error_msg_content_equals(
            "timeout must be a number greater than 0",
            box.begin, {timeout = 0})
    end)
end
