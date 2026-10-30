local netbox = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('_stream')
        s:create_index('i')
        s = box.schema.space.create('_stream_space_cache')
        s:create_index('i')
        s = box.schema.space.create('s')
        s:create_index('_space')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that spaces with `_stream` and `_stream_space_cache` can be accessed
-- from net.box streams.
g.test_any_spaces_can_be_accessed_from_netbox_stream = function(cg)
    local c = netbox:connect(cg.server.net_box_uri)
    local s = c:new_stream()
    t.assert_equals(s.space._stream:select{}, {})
    t.assert_equals(s.space._stream_space_cache:select{}, {})
    t.assert_equals(s.space.s.index._space:min(), nil)
end
