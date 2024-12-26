local server = require('luatest.server')
local t = require('luatest')
local net_box = require('net.box')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({
        alias = 'session_storage',
        box_cfg = {
            memtx_memory = 50 * 1024 * 1024,
            force_recovery = false,
            slab_alloc_factor = 1.1,
        },
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_session_storage = function(cg)
    local conn1 = net_box.connect(cg.server.net_box_uri)

    local session_id = conn1:eval('return box.session.id()')
    t.assert_type(session_id, 'number', 'session.id() type')

    local unknown_field = conn1:eval('return box.session.unknown_field')
    t.assert_equals(unknown_field, nil, 'no field')

    local session_storage = conn1:eval('return box.session.storage')
    t.assert_type(session_storage, 'table', 'session storage type')

    conn1:eval('box.session.storage.abc = "cde"')
    local storage_abc = conn1:eval('return box.session.storage.abc')
    t.assert_equals(storage_abc, 'cde', 'written to storage')

    conn1:eval('all = getmetatable(box.session).aggregate_storage')
    local is_equal = conn1:eval('return all[box.session.id()].abc == "cde"')
    t.assert_equals(is_equal, true, 'check metatable')

    local conn2 = net_box.connect(cg.server.net_box_uri)

    session_storage = conn2:eval('return box.session.storage')
    t.assert_type(session_storage, 'table', 'storage type')
    local abc_type = conn2:eval('return type(box.session.storage.abc)')
    t.assert_equals(abc_type, 'nil', 'empty storage')

    conn2:eval('box.session.storage.abc = "def"')
    local abc = conn2:eval('return box.session.storage.abc')
    t.assert_equals(abc, 'def', '"def" is in storage')

    local abc_is_cde = conn1:eval('return box.session.storage.abc == "cde"')
    t.assert_equals(abc_is_cde, true, 'first connection storage is updated')
    local abc_is_cde_conn1 =
        conn1:eval('return all[box.session.id()].abc == "cde"')
    t.assert_equals(abc_is_cde_conn1, true,
                    'first connection metatable is updated')
    local abc_is_def_conn2 =
        conn2:eval('return all[box.session.id()].abc == "def"')
    t.assert_equals(abc_is_def_conn2, true, 'check second connection metatable')

    local tres1 = conn1:eval([[
        t1 = {};
        for k, v in pairs(all) do
            table.insert(t1, v.abc)
        end
        return t1
    ]])

    conn1:close()
    conn2:close()

    local conn3 = net_box.connect(cg.server.net_box_uri)
    local tres2 = conn3:eval([[
        t2 = {};
        for k, v in pairs(all) do
            table.insert(t2, v.abc)
        end
        return t2
    ]])
    table.sort(tres1)
    table.sort(tres2)

    t.assert_equals(tres1[1], 'cde', 'check connection scan before closing')
    t.assert_equals(#tres2, 0, 'check connection scan after closing')
    conn3:close()
end
