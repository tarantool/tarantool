local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1, 10})
        box.iproto.override(box.iproto.type.SELECT, function(header, body)
            if body.space_id ~= box.space._vspace.id then
                return false
            end
            local data = {}
            for _, tuple in box.space._vspace:pairs() do
                tuple = tuple:totable()
                if tuple[1] == box.space.test.id then
                    tuple[7] = {
                        {name = 'a', type = 'unsigned'},
                        {name = 'b', type = 'foobar', is_fake = true},
                    }
                end
                table.insert(data, tuple)
            end
            box.iproto.send(box.session.id(), {
                request_type = box.iproto.type.OK,
                sync = header.sync,
                schema_version = header.schema_version,
            }, {data = data})
            return true
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_net_box_unknown_field_type = function()
    local conn = net.connect(g.server.net_box_uri)
    t.assert_equals(conn.space.test:format(), {
        {name = 'a', type = 'unsigned'},
        {name = 'b', type = 'foobar', is_fake = true},
    })
    local tuple = conn.space.test:get(1)
    t.assert_is_not(tuple, nil)
    t.assert_equals(tuple:tomap(), {1, 10, a = 1, b = 10})
    conn:close()
end
