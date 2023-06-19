local console = require('console')
local t = require('luatest')

local g = t.group()

-- Check that an error message with invalid UTF-8 sequences is encoded to Base64

g.test_bad_utf8_in_error_msg1 = function()
    local res = console.eval("box.error.new(box.error.ILLEGAL_PARAMS, 'bad: \x80')")
    local ref = "---\n- \"Illegal parameters, bad: \\x80\"\n...\n"
    t.assert_equals(res, ref)
end

g.test_bad_utf8_in_error_msg2 = function()
    local res = console.eval("require('net.box').self:call('bad: \x8a')")
    local ref = "---\n- error: \"Procedure 'bad: \\x8A' is not defined\"\n...\n"
    t.assert_equals(res, ref)
end
