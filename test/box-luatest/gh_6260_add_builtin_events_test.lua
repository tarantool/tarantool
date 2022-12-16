local t = require('luatest')
local net = require('net.box')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh_6260')

g.test_subscriptions_outside_box_cfg = function()
    local sys_events = {'box.id', 'box.status', 'box.election', 'box.schema'}

    for _, val in ipairs(sys_events) do
        local result = {}
        local result_no = 0
        local watcher = box.watch(val,
                                  function(name, state)
                                      t.assert_equals(name, val)
                                      result = state
                                      result_no = result_no + 1
                                  end)

        t.helpers.retrying({}, function() t.assert_equals(result_no, 1) end)
        t.assert_equals(result, {})
        watcher:unregister()
    end
end

g.before_test('test_sys_events_no_override', function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_server({alias = 'master'})
    cg.cluster:add_server(cg.master)
    cg.cluster:start()
end)

g.after_test('test_sys_events_no_override', function(cg)
    cg.cluster:drop()
end)

g.test_sys_events_no_override = function(cg)
    local sys_events = {'box.id', 'box.status', 'box.election', 'box.schema'}

    for _, val in ipairs(sys_events) do
        t.assert_error_msg_content_equals("System event can't be override",
        function()
            cg.master:exec(function(key)
                box.broadcast(key, 'any_data')
            end, {val})
        end)
    end
end

g.before_test('test_box_status', function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_server({alias = 'master'})
    cg.cluster:add_server(cg.master)
    cg.cluster:start()
end)

g.after_test('test_box_status', function(cg)
    cg.cluster:drop()
end)

g.test_box_status = function(cg)
    local c = net.connect(cg.master.net_box_uri)

    local result = {}
    local result_no = 0
    local watcher = c:watch('box.status',
                            function(name, state)
                                t.assert_equals(name, 'box.status')
                                result = state
                                result_no = result_no + 1
                            end)

    -- initial state should arrive
    t.helpers.retrying({}, function() t.assert_equals(result_no, 1) end)

    t.assert_equals(result,
                    {is_ro = false, is_ro_cfg = false, status = 'running'})

    -- test orphan status appearance
    cg.master:exec(function(repl)
        box.cfg{
            replication = repl,
            replication_connect_timeout = 0.001,
            replication_timeout = 0.001,
        }
    end, {{server.build_listen_uri('master'),
           server.build_listen_uri('replica')}})
    -- here we have 2 notifications: entering ro when can't connect
    -- to master and the second one when going orphan
    t.helpers.retrying({}, function() t.assert_equals(result_no, 3) end)
    t.assert_equals(result,
                    {is_ro = true, is_ro_cfg = false, status = 'orphan'})

    -- test ro_cfg appearance
    cg.master:exec(function()
        box.cfg{
            replication = {},
            read_only = true,
        }
    end)
    t.helpers.retrying({}, function() t.assert_equals(result_no, 4) end)
    t.assert_equals(result,
                    {is_ro = true, is_ro_cfg = true, status = 'running'})

    -- reset to rw
    cg.master:exec(function()
        box.cfg{
            read_only = false,
        }
    end)
    t.helpers.retrying({}, function() t.assert_equals(result_no, 5) end)
    t.assert_equals(result,
                    {is_ro = false, is_ro_cfg = false, status = 'running'})

    -- turning manual election mode puts into ro
    cg.master:exec(function()
        box.cfg{
            election_mode = 'manual',
        }
    end)
    t.helpers.retrying({}, function() t.assert_equals(result_no, 6) end)
    t.assert_equals(result,
                    {is_ro = true, is_ro_cfg = false, status = 'running'})

    -- promotion should turn rm
    cg.master:exec(function() box.ctl.promote() end)
    t.helpers.retrying({}, function() t.assert_equals(result_no, 7) end)
    t.assert_equals(result,
                    {is_ro = false, is_ro_cfg = false, status = 'running'})

    watcher:unregister()
    c:close()
end

g.before_test('test_box_election', function(cg)
    cg.cluster = cluster:new({})

    local box_cfg = {
        replication = {
            server.build_listen_uri('instance_1'),
            server.build_listen_uri('instance_2'),
            server.build_listen_uri('instance_3'),
        },
        replication_connect_quorum = 0,
        election_mode = 'off',
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 1,
        replication_timeout = 0.25,
        election_timeout = 0.25,
    }

    cg.instance_1 = cg.cluster:build_server(
        {alias = 'instance_1', box_cfg = box_cfg})

    cg.instance_2 = cg.cluster:build_server(
        {alias = 'instance_2', box_cfg = box_cfg})

    cg.instance_3 = cg.cluster:build_server(
        {alias = 'instance_3', box_cfg = box_cfg})

    cg.cluster:add_server(cg.instance_1)
    cg.cluster:add_server(cg.instance_2)
    cg.cluster:add_server(cg.instance_3)
    cg.cluster:start()
end)

g.after_test('test_box_election', function(cg)
    cg.cluster:drop()
end)

g.test_box_election = function(cg)
    local c = {}
    c[1] = net.connect(cg.instance_1.net_box_uri)
    c[2] = net.connect(cg.instance_2.net_box_uri)
    c[3] = net.connect(cg.instance_3.net_box_uri)

    local res = {}
    local res_n = {0, 0, 0}

    for i = 1, 3 do
        c[i]:watch('box.election',
                   function(n, s)
                       t.assert_equals(n, 'box.election')
                       res[i] = s
                       res_n[i] = res_n[i] + 1
                   end)
    end
    t.helpers.retrying({}, function()
        t.assert_equals(res_n[1] + res_n[2] + res_n[3], 3)
    end)

    -- verify all instances are in the same state
    t.assert_equals(res[1], res[2])
    t.assert_equals(res[1], res[3])

    -- wait for elections to complete, verify leader is the instance_1
    -- trying to avoid the exact number of term - it can vary
    local instance1_id = cg.instance_1:get_instance_id()

    cg.instance_1:exec(function() box.cfg{election_mode='candidate'} end)
    cg.instance_2:exec(function() box.cfg{election_mode='voter'} end)
    cg.instance_3:exec(function() box.cfg{election_mode='voter'} end)

    cg.instance_1:wait_until_election_leader_found()
    cg.instance_2:wait_until_election_leader_found()
    cg.instance_3:wait_until_election_leader_found()

    t.assert_covers(res[1],
                    {leader = instance1_id, is_ro = false, role = 'leader'})
    t.assert_covers(res[2],
                    {leader = instance1_id, is_ro = true, role = 'follower'})
    t.assert_covers(res[3],
                    {leader = instance1_id, is_ro = true, role = 'follower'})

    -- verify all terms are in the same state
    t.assert_equals(res[1].term, res[2].term)
    t.assert_equals(res[1].term, res[3].term)

    -- check the stepping down is working
    res_n = {0, 0, 0}
    cg.instance_1:exec(function() box.cfg{election_mode='voter'} end)
    t.helpers.retrying({}, function()
        t.assert_equals(res_n[1] + res_n[2] + res_n[3], 3)
    end)

    local expected = {is_ro = true, role = 'follower', term = res[1].term,
                      leader = 0}
    t.assert_covers(res, {expected, expected, expected})

    c[1]:close()
    c[2]:close()
    c[3]:close()
end

g.before_test('test_box_schema', function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_server({alias = 'master'})
    cg.cluster:add_server(cg.master)
    cg.cluster:start()
end)

g.after_test('test_box_schema', function(cg)
    cg.cluster:drop()
end)

g.test_box_schema = function(cg)
    local c = net.connect(cg.master.net_box_uri)
    local version = 0
    local version_n = 0

    local watcher = c:watch('box.schema',
                            function(n, s)
                                t.assert_equals(n, 'box.schema')
                                version = s.version
                                version_n = version_n + 1
                            end)

    cg.master:exec(function() box.schema.create_space('p') end)
    t.helpers.retrying({}, function() t.assert_equals(version_n, 2) end)
    -- there'll be 2 changes - index and space
    -- first version change, use it as initial value
    local init_version = version

    version_n = 0
    cg.master:exec(function() box.space.p:create_index('i') end)
    t.helpers.retrying({}, function() t.assert_equals(version_n, 1) end)
    t.assert_equals(version, init_version + 1)

    version_n = 0
    cg.master:exec(function() box.space.p:drop() end)
    t.helpers.retrying({}, function() t.assert_equals(version_n, 2) end)
    -- there'll be 2 changes - index and space
    t.assert_equals(version, init_version + 3)

    watcher:unregister()
    c:close()
end

g.before_test('test_box_id', function(cg)
    cg.cluster = cluster:new({})

    local box_cfg = {
        replicaset_uuid = t.helpers.uuid('ab', 1),
        instance_uuid = t.helpers.uuid('1', '2', '3')
    }

    cg.instance = cg.cluster:build_server({alias = 'master', box_cfg = box_cfg})
    cg.cluster:add_server(cg.instance)
    cg.cluster:start()
end)

g.after_test('test_box_id', function(cg)
    cg.cluster:drop()
end)

g.test_box_id = function(cg)
    local c = net.connect(cg.instance.net_box_uri)

    local result = {}
    local result_no = 0

    local watcher = c:watch('box.id',
                            function(name, state)
                                t.assert_equals(name, 'box.id')
                                result = state
                                result_no = result_no + 1
                            end)

    -- initial state should arrive
    t.helpers.retrying({}, function() t.assert_equals(result_no, 1) end)
    t.assert_equals(result, {id = 1,
                    instance_uuid = cg.instance.box_cfg.instance_uuid,
                    replicaset_uuid = cg.instance.box_cfg.replicaset_uuid})

    watcher:unregister()
    c:close()
end
