local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'pcall'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- #267: Bad exception catching.
g.test_pcall_inside_xpcall = function()
    g.server:exec(function()
        local function pcalltest()
            local ERRMSG = "module 'some_invalid_module' not found"
            local status, msg = pcall(require, 'some_invalid_module')
            if status == false and msg ~= nil and msg:match(ERRMSG) ~= nil then
                return 'pcall is ok'
            else
                return 'pcall is broken'
            end
        end

        local status, msg = xpcall(pcalltest, function(msg)
            error('error handler: ' .. msg)
        end)
        t.assert_equals(status, true, 'pcall inside xpcall, status')
        t.assert_equals(msg, 'pcall is ok',
                        'pcall inside xpcall, error message')
    end)
end

g.test_pcall_with_lua_error = function()
    g.server:exec(function()
        local fn = function() error('some message') end
        t.assert_error_msg_contains('some message', fn)
    end)
end

g.test_pcall_with_box_error = function()
    g.server:exec(function()
        local ffi = require('ffi')
        local status, msg = pcall(function()
            box.error(box.error.ILLEGAL_PARAMS, 'some message')
        end)

        t.assert_equals(status, false, 'status')
        t.assert_equals(tostring(ffi.typeof(msg)),
                        'ctype<const struct error &>', 'typeof')
        t.assert_equals(msg.type, 'IllegalParams', 'pcall with box error, type')
        t.assert_equals(msg.message, 'some message',
                        'pcall with box error, message')
        t.assert_equals(msg:match('some'), 'some',
                        'pcall with box error, message')
    end)
end

g.test_pcall_with_no_return = function()
    g.server:exec(function()
        t.assert_equals(select('#', pcall(function() end)), 1,
                        'pcall with no return')
    end)
end

g.test_pcall_with_multireturn = function()
    g.server:exec(function()
        local ok, res1, res2, res3 = pcall(function() return 1, 2, 3 end)
        t.assert_equals(ok, true, 'pcall with multireturn, status')
        t.assert_equals(res1, 1, 'pcall with multireturn, result 1')
        t.assert_equals(res2, 2, 'pcall with multireturn, result 2')
        t.assert_equals(res3, 3, 'pcall with multireturn, result 3')
    end)
end
