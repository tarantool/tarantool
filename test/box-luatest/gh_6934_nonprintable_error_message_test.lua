local console = require('console')
local t = require('luatest')

local g = t.group()

-- Check that an error message with invalid UTF-8 sequences is encoded to Base64

g.test_bad_utf8_in_error_msg1 = function()
    local res = console.eval("box.error.new(box.error.ILLEGAL_PARAMS, 'bad: \x80')")
    local ref = "---\n- !!binary SWxsZWdhbCBwYXJhbWV0ZXJzLCBiYWQ6IIA=\n...\n"
    t.assert_equals(res, ref)
end

g.test_bad_utf8_in_error_msg2 = function()
    local res = console.eval("require('net.box').self:call('bad: \x8a')")
    local ref = "---\n- error: !!binary UHJvY2VkdXJlICdiYWQ6IIonIGlzIG5vdCBkZWZpbmVk\n...\n"
    t.assert_equals(res, ref)
end
