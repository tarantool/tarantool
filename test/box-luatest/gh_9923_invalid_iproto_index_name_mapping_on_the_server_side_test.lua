local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.create_space('test', {
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
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    -- fetch schema = false to resolve index on server instead of client
    g.conn = net.connect(g.server.net_box_uri, {fetch_schema = false})

    g.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_each(function()
    g.conn:close()
end)

g.test_index_select = function()
    local space = g.conn.space.test
    t.assert_is_not(space, nil)

    space:insert({1, 'F'})

    local res1 = space.index.secondary:select('F')
    local res2 = space.index[5]:select('F')

    t.assert_equals(res1, {{1, 'F'}})
    t.assert_equals(res1, res2)
end

g.test_index_delete = function()
    local space = g.conn.space.test
    t.assert_is_not(space, nil)

    space:insert({2, 'A'})

    local deleted_by_name = space.index.secondary:delete('A')
    t.assert_equals(deleted_by_name, {2, 'A'})

    local check_deleted_by_name = space:get(2)
    t.assert_is(check_deleted_by_name, nil)
end

g.test_index_get = function()
    local space = g.conn.space.test
    t.assert_is_not(space, nil)

    space:insert({4, 'C'})

    local fetched_by_name = space.index.secondary:get('C')
    t.assert_equals(fetched_by_name, {4, 'C'})

    local check_fetched_by_id = space:get(4)
    t.assert_equals(check_fetched_by_id, {4, 'C'})
end
