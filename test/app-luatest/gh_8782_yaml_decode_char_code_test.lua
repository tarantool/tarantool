local yaml = require('yaml')

local t = require('luatest')
local g = t.group()

g.test_decode_code_1 = function()
    for code = tonumber('00', 16), tonumber('ff', 16) do
        local input = string.format('"\\x%02x"', code)
        local msg = 'decode ' .. input
        local expected = string.char(code)
        local ok, result = pcall(yaml.decode, input)
        t.assert(ok, msg)
        t.assert_equals(string.hex(result), string.hex(expected), msg)
    end
end

g.test_decode_code_2 = function()
    for code = 0, tonumber('ffff', 16), 10 do
        local input = string.format('"\\u%04x"', code)
        local msg = 'decode ' .. input
        if code >= tonumber('d800', 16) and code <= tonumber('dfff', 16) then
            t.assert_error_msg_contains(
                'found invalid Unicode character escape code',
                yaml.decode, input)
        else
            local expected = utf8.char(code)
            local ok, result = pcall(yaml.decode, input)
            t.assert(ok, msg)
            t.assert_equals(string.hex(result), string.hex(expected), msg)
        end
    end
end

g.test_decode_code_4 = function()
    for code = 0, tonumber('ffffff', 16), 1000 do
        local input = string.format('"\\U%08x"', code)
        local msg = 'decode ' .. input
        if (code >= tonumber('d800', 16) and code <= tonumber('dfff', 16)) or
                code >= tonumber('10ffff', 16) then
            t.assert_error_msg_contains(
                'found invalid Unicode character escape code',
                yaml.decode, input)
        else
            local expected = utf8.char(code)
            local ok, result = pcall(yaml.decode, input)
            t.assert(ok, msg)
            t.assert_equals(string.hex(result), string.hex(expected), msg)
        end
    end
end
