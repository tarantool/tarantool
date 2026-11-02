-- Common utils. This code may be executed in servers context.

local t = require('luatest')
local json = require('json')

local M = {}

function M.check_ssl_error_msg(error_msg, f, ...)
    local ok, err = pcall(f, ...)
    assert(not ok, 'error expected')
    if type(error_msg) == 'string' then
        t.assert_str_matches(err.message, error_msg)
    elseif type(error_msg) == 'table' then
        t.fail_if(type(error_msg.ssl_c) ~= 'string', 'misconfig')
        t.fail_if(type(error_msg.openssl_cases) ~= 'table', 'misconfig')
        t.assert_str_contains(err.message, error_msg.ssl_c)
        local found = false
        for _, e in ipairs(error_msg.openssl_cases) do
            if string.find(err.message, e) ~= nil then
                found = true
                break
            end
        end
        t.assert(found,
            string.format("'%s' matches one of %s",
                err.message, json.encode(error_msg.openssl_cases)))
    else
        t.fail('misconfig')
    end
end

return M
