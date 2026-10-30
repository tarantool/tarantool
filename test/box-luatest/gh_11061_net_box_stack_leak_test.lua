local t = require('luatest')
local server = require('luatest.server')
local net_box_lib = require('net.box.lib')

local g = t.group()

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

local function check_reconnect_stack(cg, callback)
    cg.error_ref = setmetatable({}, {__mode = 'v'})
    cg.phase = 'initial'
    cg.transport = net_box_lib.new_transport(cg.server.net_box_uri, nil, nil,
                                            callback, nil, 0.001, false, nil)
    cg.transport:start()
    t.helpers.retrying({}, function()
        t.assert_equals(cg.phase, 'recovered')
    end)
    collectgarbage('collect')
    collectgarbage('collect')
    t.assert_equals(cg.error_ref.value, nil)
end

--
-- Errors from 'active' callbacks exercise stack cleanup in netbox_worker_f().
--
g.test_active = function(cg)
    local function callback(what, state)
        if what ~= 'state_changed' or state ~= 'active' then
            return
        end
        if cg.phase ~= 'initial' then
            cg.phase = 'recovered'
            return
        end
        local error_object = {}
        cg.error_ref.value = error_object
        cg.phase = 'injected'
        error(error_object, 0)
    end
    check_reconnect_stack(cg, callback)
end

--
-- Errors from 'error_reconnect' callbacks exercise stack cleanup in
-- netbox_transport_on_state_change_pcall().
--
g.test_error_reconnect = function(cg)
    local function callback(what, state)
        if what ~= 'state_changed' then
            return
        end
        if state == 'active' then
            if cg.phase == 'initial' then
                error('force reconnect', 0)
            end
            cg.phase = 'recovered'
        elseif state == 'error_reconnect' then
            local error_object = {}
            cg.error_ref.value = error_object
            cg.phase = 'injected'
            error(error_object, 0)
        end
    end
    check_reconnect_stack(cg, callback)
end
