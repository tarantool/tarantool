local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()
local net = require('net.box')

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.create_space('space1'):create_index('primary')
    end)
end

g.after_all = function()
    g.server:stop()
end

local function tabcomplete(s)
    local t = {}
    for _, v in pairs(_G.package.loaded.console.completion_handler(s, 0, #s)) do
        t[v] = true
    end
    return t
end

g.test_autocomplete = function()
    local c = net:connect(g.server.net_box_uri)
    -- tabcomplete always uses global table
    rawset(_G, 'conn1', c)

    -- connection should provide all functions available
    local r = tabcomplete('conn1:')
    t.assert_equals(r, {['conn1:'] = true,
                        ['conn1:eval_16('] = true,
                        ['conn1:call('] = true,
                        ['conn1:reload_schema('] = true,
                        ['conn1:on_disconnect('] = true,
                        ['conn1:on_shutdown('] = true,
                        ['conn1:wait_connected('] = true,
                        ['conn1:watch('] = true,
                        ['conn1:call_16('] = true,
                        ['conn1:execute('] = true,
                        ['conn1:prepare('] = true,
                        ['conn1:wait_state('] = true,
                        ['conn1:unprepare('] = true,
                        ['conn1:close('] = true,
                        ['conn1:on_connect('] = true,
                        ['conn1:ping('] = true,
                        ['conn1:new_stream('] = true,
                        ['conn1:is_connected('] = true,
                        ['conn1:eval('] = true,
                        ['conn1:on_schema_reload('] = true,
                        })

    -- it should return all spaces and indexes
    r = tabcomplete('conn1.space.s')
    t.assert_equals(r, {['conn1.space.space1'] = true,
                        })

    r = tabcomplete('conn1.space.space1.ind')
    t.assert_equals(r, {['conn1.space.space1.index'] = true,
                        })

    r = tabcomplete('conn1.space.space1.index.p')
    t.assert_equals(r, {['conn1.space.space1.index.primary'] = true,
                        })

    r = tabcomplete('conn1.space.space1.index.primary:')
    t.assert_equals(r, {['conn1.space.space1.index.primary:'] = true,
                        ['conn1.space.space1.index.primary:max('] = true,
                        ['conn1.space.space1.index.primary:select('] = true,
                        ['conn1.space.space1.index.primary:update('] = true,
                        ['conn1.space.space1.index.primary:delete('] = true,
                        ['conn1.space.space1.index.primary:count('] = true,
                        ['conn1.space.space1.index.primary:min('] = true,
                        ['conn1.space.space1.index.primary:get('] = true,
                        })

    -- sreams are pretty the same
    local s = c:new_stream()
    rawset(_G, 'stream1', s)
    r = tabcomplete('stream1.space.s')
    t.assert_equals(r, {['stream1.space.space1'] = true,
                        })

    r = tabcomplete('stream1.space.space1.ind')
    t.assert_equals(r, {['stream1.space.space1.index'] = true,
                        })

    r = tabcomplete('stream1.space.space1.index.p')
    t.assert_equals(r, {['stream1.space.space1.index.primary'] = true,
                        })

    r = tabcomplete('stream1.space.space1.index.primary:')
    t.assert_equals(r, {['stream1.space.space1.index.primary:'] = true,
                        ['stream1.space.space1.index.primary:max('] = true,
                        ['stream1.space.space1.index.primary:select('] = true,
                        ['stream1.space.space1.index.primary:update('] = true,
                        ['stream1.space.space1.index.primary:delete('] = true,
                        ['stream1.space.space1.index.primary:count('] = true,
                        ['stream1.space.space1.index.primary:min('] = true,
                        ['stream1.space.space1.index.primary:get('] = true,
                        })

    -- futures
    local f = c.space.space1:select({}, {is_async = true})
    rawset(_G, 'future1', f)
    r = tabcomplete('future1:')
    t.assert_equals(r, {['future1:'] = true,
                        ['future1:wait_result('] = true,
                        ['future1:result('] = true,
                        ['future1:discard('] = true,
                        ['future1:pairs('] = true,
                        ['future1:is_ready('] = true,
                        })

end
