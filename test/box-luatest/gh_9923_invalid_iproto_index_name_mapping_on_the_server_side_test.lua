local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test', {
            id = 621,
            temporary = true,
            if_not_exists = true,
            field_count = 3,
            format = {
                {name = "id", type = "unsigned"},
                {name = "name", type = "string"},
                {name = "arr1", type = "array"},
            },
        })
        box.space.test:create_index('primary', {
            type = 'tree',
            unique = true,
            parts = {1, 'unsigned'},
            if_not_exists = true,
        })
        box.space.test:create_index('secondary', {
            id = 5,
            type = 'tree',
            unique = false,
            parts = {2, 'string'},
            if_not_exists = true,
        })
        box.space.test:create_index('unique_secondary', {
            id = 6,
            type = 'tree',
            unique = true,
            parts = {2, 'string'},
            if_not_exists = true,
        })
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_index_access_errors = function()
    -- fetch schema = false to resolve index on server instead of client
    local conn = net.connect(g.server.net_box_uri, {fetch_schema = false})
    t.assert_is_not(conn, nil)

    local space = conn.space.test
    t.assert_is_not(space, nil)

    local res1 = space.index.secondary:select()
    local res2 = space.index[5]:select()
    t.assert_equals(res1, res2)

    conn:close()
end

g.test_index_delete = function()
    local conn = net.connect(g.server.net_box_uri, {fetch_schema = false})
    t.assert_is_not(conn, nil)

    local space = conn.space.test
    t.assert_is_not(space, nil)

    space:insert({2, 'A', {1, 2, 3}})
    space:insert({4, 'B', {4, 5, 6}})
    space:insert({8, 'C', {7, 8, 9}})

    local deleted_by_name = space.index.unique_secondary:delete('A')
    t.assert_equals(deleted_by_name, {2, 'A', {1, 2, 3}})

    local check_deleted_by_name = space:get(2)
    t.assert_is(check_deleted_by_name, nil)

    local deleted_by_id = space.index[6]:delete('C')
    t.assert_equals(deleted_by_id, {8, 'C', {7, 8, 9}})

    local check_deleted_by_id = space:get(8)
    t.assert_is(check_deleted_by_id, nil)

    conn:close()
end
