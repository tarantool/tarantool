local t = require('luatest')
local server = require('luatest.server')
local net_box_lib = require('net.box.lib')

local g = t.group(nil, t.helpers.matrix({
    error_type = {'lua', 'box'},
    error_state = {'active', 'error_reconnect'},
}))

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.transport ~= nil then
        cg.transport:stop(true)
        cg.transport = nil
    end
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_reconnect_stack = function(cg)
    local errors = setmetatable({}, {__mode = 'v'})
    local attempts = 0
    local active = false
    local last_error
    local message = 'net.box stack leak test'
    local function callback(what, state, err)
        if what ~= 'state_changed' then
            return
        end
        if state == 'active' then
            attempts = attempts + 1
            if attempts > 5 then
                active = true
                return
            end
            if cg.params.error_state ~= state then
                error('force reconnect', 0)
            end
        elseif state == 'error_reconnect' then
            last_error = err.message
        end
        if cg.params.error_state == state then
            local error_object
            if cg.params.error_type == 'box' then
                error_object = box.error.new(box.error.PROC_LUA, message)
            else
                error_object = setmetatable({}, {
                    __tostring = function() return message end,
                })
            end
            errors[attempts] = error_object
            error(error_object, 0)
        end
    end
    cg.transport = net_box_lib.new_transport(cg.server.net_box_uri, nil, nil,
                                            callback, nil, 0.001, false, nil)
    cg.transport:start()
    t.helpers.retrying({}, function() t.assert(active) end)
    t.assert_equals(attempts, 6)
    t.assert_equals(last_error, cg.params.error_state == 'active' and message or
                    'force reconnect')
    collectgarbage('collect')
    collectgarbage('collect')
    t.assert_equals(next(errors), nil)
end
