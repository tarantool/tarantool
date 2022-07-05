local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group('gh_6126')

g.before_each(function(cg)
    cg.cluster = cluster:new({})

    local box_cfg = {
        replication = {
            server.build_listen_uri('instance_1', cg.cluster.id),
        },
        instance_uuid = t.helpers.uuid('b'),
    }
    cg.instance_1 = cg.cluster:build_and_add_server(
        {alias = 'instance_1', box_cfg = box_cfg})

    box_cfg = {
        replication = {
            server.build_listen_uri('anon', cg.cluster.id),
            cg.instance_1.net_box_uri,
        },
        instance_uuid = t.helpers.uuid('a'),
        read_only = true,
        replication_anon = true,
    }
    cg.anon = cg.cluster:build_and_add_server(
        {alias = 'anon', box_cfg = box_cfg})

    box_cfg = {
        replication = {
            cg.anon.net_box_uri,
            cg.instance_1.net_box_uri,
            server.build_listen_uri('instance_2', cg.cluster.id),
        },
        instance_uuid = t.helpers.uuid('c'),
    }
    cg.instance_2 = cg.cluster:build_and_add_server(
        {alias = 'instance_2', box_cfg = box_cfg})
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_auto_rejoin = function(cg)
    cg.instance_1:start()
    cg.instance_1:exec(function() box.cfg{read_only = true} end)
    cg.anon:start()
    cg.instance_2:start({wait_until_ready = false})

    local logfile = fio.pathjoin(cg.instance_2.workdir, 'instance_2.log')
    t.helpers.retrying({}, function()
        t.assert(cg.instance_2:grep_log('ER_UNSUPPORTED', nil,
            {filename = logfile}), 'Anonymous replica does not support'..
            'registration of non-anonymous nodes.')
        t.assert(cg.instance_2:grep_log('rebootstrap failed, will '..
            'retry every 1.00 second', nil, {filename = logfile}),
            'reboootstrap failed')
    end)
    t.assert(cg.instance_1:exec(function()
        return box.space._cluster:count() == 1
    end),  'No join while instance_1 is read-only')

    cg.instance_1:exec(function() box.cfg{read_only = false} end)
    cg.instance_1:exec(function()
        local s1 = box.schema.create_space('test1', {engine = 'vinyl'})
        s1:create_index('pk')
        s1:replace{1}
        local s2 = box.schema.create_space('test2')
        s2:create_index('pk')
        s2:replace{2}
        box.snapshot()
        s1:replace{3}
        s2:replace{4}
    end)

    cg.instance_2:wait_until_ready()
    t.helpers.retrying({}, function()
        t.assert(cg.instance_1:exec(function()
            return box.space._cluster:count() == 2
        end), 'Join after instance_1 became writeable')
        cg.instance_2:assert_follows_upstream(1)
    end)

    -- Check
    t.assert_equals(cg.instance_2:exec(function()
        return box.space.test1:select()
    end), {{1}, {3}})
    t.assert_equals(cg.instance_2:exec(function()
        return box.space.test2:select()
    end), {{2}, {4}})
    t.assert_equals(cg.anon:exec(function()
        return box.space.test1:select()
    end), {{1}, {3}})
    t.assert_equals(cg.anon:exec(function()
        return box.space.test2:select()
    end), {{2}, {4}})
end
