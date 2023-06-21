local fun = require('fun')
local t = require('luatest')
local server = require('test.luatest_helpers.server')

local function start_example_replicaset(g, dir, config_file, opts)
    local credentials = {
        user = 'client',
        password = 'secret',
    }
    local opts = fun.chain({
        config_file = config_file,
        chdir = dir,
        net_box_credentials = credentials,
    }, opts or {}):tomap()
    g.server_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.server_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})

    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()

    local info = g.server_1:eval('return box.info')
    t.assert_equals(info.name, 'instance-001')
    t.assert_equals(info.replicaset.name, 'replicaset-001')
    t.assert_equals(info.cluster.name, 'group-001')

    local info = g.server_2:eval('return box.info')
    t.assert_equals(info.name, 'instance-002')
    t.assert_equals(info.replicaset.name, 'replicaset-001')
    t.assert_equals(info.cluster.name, 'group-001')

    local info = g.server_3:eval('return box.info')
    t.assert_equals(info.name, 'instance-003')
    t.assert_equals(info.replicaset.name, 'replicaset-001')
    t.assert_equals(info.cluster.name, 'group-001')
end

return {
    start_example_replicaset = start_example_replicaset,
}
