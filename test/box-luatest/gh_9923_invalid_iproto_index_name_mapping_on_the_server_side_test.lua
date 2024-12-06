local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        box.schema.create_space('test', {
            id = 621,
            format = {
                {name = "id", type = "unsigned"},
                {name = "name", type = "string"},
            },
        })
        box.space.test:create_index('primary', {
            type = 'tree',
            unique = true,
            parts = {1, 'unsigned'},
        })
        box.space.test:create_index('secondary', {
            id = 5,
            type = 'tree',
            unique = true,
            parts = {2, 'string'},
        })
    end)
    cg.conn = net.connect(cg.server.net_box_uri, {fetch_schema = false})
    t.assert_is_not(g.conn, nil)
end)

g.after_each(function()
    g.conn:close()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_index_select = function(cg)
    local space = cg.conn.space.test
    t.assert_is_not(space, nil)

    local res1 = space.index.secondary:select()
    local res2 = space.index[5]:select()
    t.assert_equals(res1, res2)
end

g.test_index_delete = function(cg)
    local space = cg.conn.space.test
    t.assert_is_not(space, nil)

    space:insert({2, 'A'})
    space:insert({4, 'B'})
    space:insert({8, 'C'})

    local deleted_by_name = space.index.secondary:delete('A')
    t.assert_equals(deleted_by_name, {2, 'A'})

    local check_deleted_by_name = space:get(2)
    t.assert_is(check_deleted_by_name, nil)
end
