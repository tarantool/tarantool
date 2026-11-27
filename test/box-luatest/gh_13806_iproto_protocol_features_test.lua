local t = require('luatest')
local server = require('luatest.server')
local netbox = require('net.box')

local g = t.group()
local app_threads = 2

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {app_threads = app_threads},
        net_box_credentials = {user = 'admin'},
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                local t = require('luatest')
                local compat = require('compat')
                local features = box.iproto.protocol_features
                compat.box_tuple_extension = 'old'
                t.assert_not(features.call_ret_tuple_extension)
                t.assert_not(features.call_arg_tuple_extension)
                compat.box_tuple_extension = 'new'
                t.assert(features.call_ret_tuple_extension)
                t.assert(features.call_arg_tuple_extension)
                compat.box_tuple_extension = 'old'
                -- References cached before box.cfg() must remain valid.
                t.assert_is(features, box.iproto.protocol_features)
            ]],
        },
    })
    cg.server:start()
    for thread_id = 0, app_threads do
        cg.server:exec(function()
            rawset(_G, 'saved_protocol_features', box.iproto.protocol_features)
        end, {}, {_thread_id = thread_id})
    end
end)

g.after_all(function(cg)
    cg.server:drop()
end)

local function get_features(cg, thread_id)
    return cg.server:exec(function()
        -- Reloads must update the table cached by applications.
        t.assert_is(box.iproto.protocol_features, _G.saved_protocol_features)
        return box.iproto.protocol_features
    end, {}, {_thread_id = thread_id})
end

--
-- Application threads must see the configured features as soon as box.cfg()
-- returns, without waiting for a later compatibility change.
--
g.test_startup = function(cg)
    local expected = get_features(cg, 0)
    t.assert_equals(expected.call_ret_tuple_extension, nil)
    t.assert_equals(expected.call_arg_tuple_extension, nil)
    for thread_id = 1, app_threads do
        t.assert_equals(get_features(cg, thread_id), expected)
    end
end

--
-- Cached references in every Lua state must agree with the features advertised
-- to fresh connections after both enabling and disabling tuple extensions.
--
g.test_reload = function(cg)
    for _, mode in ipairs({'new', 'old'}) do
        cg.server:exec(function(new_mode)
            require('compat').box_tuple_extension = new_mode
        end, {mode})
        local expected = get_features(cg, 0)
        local enabled = mode == 'new' and true or nil
        t.assert_equals(expected.call_ret_tuple_extension, enabled)
        t.assert_equals(expected.call_arg_tuple_extension, enabled)

        local conn = netbox.connect(cg.server.net_box_uri,
                                    {fetch_schema = false})
        t.assert(conn:is_connected(), conn.error)
        local advertised = {}
        for name, enabled in pairs(conn.peer_protocol_features) do
            if enabled then
                advertised[name] = true
            end
        end
        conn:close()
        t.assert_equals(expected, advertised)

        for thread_id = 1, app_threads do
            t.helpers.retrying({}, function()
                t.assert_equals(get_features(cg, thread_id), expected)
            end)
        end
    end
end
