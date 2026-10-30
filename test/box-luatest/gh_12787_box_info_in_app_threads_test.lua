local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {app_threads = 1},
        net_box_credentials = {user = 'admin'}
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_box_info = function(cg)
    local main_box_info = cg.server:exec(function()
        local tarantool = require('tarantool')
        t.assert_equals(box.info, {
            package = tarantool.package,
            version = tarantool.version,
        })
        return box.info()
    end)
    local box_info = cg.server:exec(function(main_box_info)
        local yaml = require('yaml')
        local tarantool = require('tarantool')
        t.assert_equals(box.info, {
            package = tarantool.package,
            version = tarantool.version,
        })
        local expected = {}
        for _, k in ipairs({
            'package', 'version', 'id', 'uuid', 'name',
            'replicaset', 'cluster',
        }) do
            t.assert_is_not(main_box_info[k], nil)
            expected[k] = main_box_info[k]
        end
        local box_info = box.info()
        t.assert_equals(box_info, expected)
        t.assert_equals(yaml.decode(yaml.encode(box.info)), expected)
        for k, v in pairs(expected) do
            t.assert_equals(box_info[k], v, k)
        end
        return box_info
    end, {main_box_info}, {_thread_id = 1})
    --
    -- Set instance, replicaset, and cluster names and check that
    -- they are propagated to application threads.
    --
    cg.server:exec(function()
        box.cfg({
            instance_name = 'test_instance',
            replicaset_name = 'test_replicaset',
            cluster_name = 'test_cluster',
        })
        t.assert_covers(box.info(), {
            name = 'test_instance',
            replicaset = {name = 'test_replicaset'},
            cluster = {name = 'test_cluster'},
        })
    end)
    box_info.name = 'test_instance'
    box_info.replicaset.name = 'test_replicaset'
    box_info.cluster.name = 'test_cluster'
    cg.server:exec(function(box_info)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info(), box_info)
        end)
    end, {box_info}, {_thread_id = 1})
    --
    -- Check that box.info() is restored after restart.
    --
    cg.server:restart()
    cg.server:exec(function(box_info)
        t.assert_equals(box.info(), box_info)
    end, {box_info}, {_thread_id = 1})
end
