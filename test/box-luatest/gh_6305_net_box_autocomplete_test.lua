local server = require('luatest.server')
local t = require('luatest')
local net = require('net.box')
local console = require('console')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.create_space('space1'):create_index('primary')
    end)
end)

g.after_all(function()
    g.server:stop()
end)

local function tabcomplete(s)
    return console.completion_handler(s, 0, #s)
end

g.test_autocomplete = function()
    local c = net:connect(g.server.net_box_uri)
    -- tabcomplete always uses global table
    rawset(_G, 'conn1', c)

    -- connection should provide all functions available
    local r = tabcomplete('conn1:')
    t.assert_items_equals(r, {'conn1:',
                              'conn1:call(',
                              'conn1:reload_schema(',
                              'conn1:on_disconnect(',
                              'conn1:on_shutdown(',
                              'conn1:wait_connected(',
                              'conn1:watch(',
                              'conn1:watch_once(',
                              'conn1:execute(',
                              'conn1:wait_state(',
                              'conn1:ping(',
                              'conn1:unprepare(',
                              'conn1:prepare(',
                              'conn1:close(',
                              'conn1:on_connect(',
                              'conn1:new_stream(',
                              'conn1:is_connected(',
                              'conn1:eval(',
                              'conn1:on_schema_reload(',
                              })

    -- it should return all spaces and indexes
    r = tabcomplete('conn1.space.s')
    t.assert_items_equals(r, {'conn1.space.space1',
                              'conn1.space.space1',
                              })

    r = tabcomplete('conn1.space.space1.ind')
    t.assert_items_equals(r, {'conn1.space.space1.index',
                              'conn1.space.space1.index',
                              })

    r = tabcomplete('conn1.space.space1.index.p')
    t.assert_items_equals(r, {'conn1.space.space1.index.primary',
                              'conn1.space.space1.index.primary',
                              })

    r = tabcomplete('conn1.space.space1.index.primary:')
    t.assert_items_equals(r, {'conn1.space.space1.index.primary:',
                              'conn1.space.space1.index.primary:max(',
                              'conn1.space.space1.index.primary:select(',
                              'conn1.space.space1.index.primary:update(',
                              'conn1.space.space1.index.primary:delete(',
                              'conn1.space.space1.index.primary:count(',
                              'conn1.space.space1.index.primary:min(',
                              'conn1.space.space1.index.primary:get(',
                              })

    -- sreams are pretty the same
    local s = c:new_stream()
    rawset(_G, 'stream1', s)
    r = tabcomplete('stream1.space.s')
    t.assert_items_equals(r, {'stream1.space.space1',
                              'stream1.space.space1',
                              })

    r = tabcomplete('stream1.space.space1.ind')
    t.assert_items_equals(r, {'stream1.space.space1.index',
                              'stream1.space.space1.index',
                              })

    r = tabcomplete('stream1.space.space1.index.p')
    t.assert_items_equals(r, {'stream1.space.space1.index.primary',
                              'stream1.space.space1.index.primary',
                              })

    r = tabcomplete('stream1.space.space1.index.primary:')
    t.assert_items_equals(r, {'stream1.space.space1.index.primary:',
                              'stream1.space.space1.index.primary:max(',
                              'stream1.space.space1.index.primary:select(',
                              'stream1.space.space1.index.primary:update(',
                              'stream1.space.space1.index.primary:delete(',
                              'stream1.space.space1.index.primary:count(',
                              'stream1.space.space1.index.primary:min(',
                              'stream1.space.space1.index.primary:get(',
                              })

    -- futures
    local f = c.space.space1:select({}, {is_async = true})
    f.user1 = 123
    rawset(_G, 'future1', f)
    r = tabcomplete('future1:')
    t.assert_items_equals(r, {'future1:',
                              'future1:wait_result(',
                              'future1:result(',
                              'future1:discard(',
                              'future1:pairs(',
                              'future1:is_ready(',
                              })
    r = tabcomplete('future1.')
    t.assert_items_equals(r, {'future1.',
                              'future1.user1',
                              'future1.wait_result(',
                              'future1.result(',
                              'future1.discard(',
                              'future1.pairs(',
                              'future1.is_ready(',
                              })
end
