local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')

local g = t.group('gh-8505-synchro-triggers')

g.before_all(function(g)
    g.replica_set = replica_set:new({})
    local rs_id = g.replica_set.id
    g.box_cfg = {
        replication_timeout = 0.01,
        replication = {
            server.build_listen_uri('server1', rs_id),
            server.build_listen_uri('server2', rs_id),
        },
    }

    g.box_cfg.election_mode = 'candidate'
    g.server1 = g.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = g.box_cfg,
    }

    g.proxy1 = proxy:new{
        client_socket_path = server.build_listen_uri('server1_proxy'),
        server_socket_path = server.build_listen_uri('server1', rs_id),
    }
    t.assert(g.proxy1:start{force = true}, 'Proxy from 2 to 1 started')
    g.box_cfg.replication[1] = server.build_listen_uri('server1_proxy')
    g.box_cfg.election_mode = 'voter'
    g.server2 = g.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = g.box_cfg,
    }

    g.replica_set:start()
    g.server1:wait_for_election_leader()
    g.server1:exec(function()
        box.schema.create_space('test', {is_sync = true}):create_index('pk')
    end)
    g.server2:wait_for_vclock_of(g.server1)
end)

g.after_all(function(g)
    g.replica_set:drop()
end)

g.test_on_commit_trigger = function(g)
    g.server1:exec(function()
        box.begin()
        box.on_commit(function(iter) iter() end)
        box.space.test:upsert({1}, {{'=', 1, 1}})
        box.commit()
    end)
end

g.test_on_rollback_trigger = function(g)
    -- Force ACK gathering to fail and cause rollback. It's not enough
    -- to set a small timeout, as a transaction can be committed anyway:
    -- fibers don't yield so often, compared to such a tiny timeout, ACKs
    -- can be processed before the transaction's rollback happens due to
    -- a timeout error. So, let's break connection with proxy.
    g.server1:update_box_cfg({ replication_synchro_timeout = 1e-9 })
    g.server1:wait_for_election_leader()
    g.proxy1:pause()

    g.server1:exec(function()
        box.begin()
        box.on_rollback(function(iter) iter() end)
        box.space.test:upsert({1}, {{'=', 1, 1}})
        local _, err = pcall(box.commit)
        t.assert_equals(err.code, box.error.SYNC_QUORUM_TIMEOUT)
    end)

    g.proxy1:resume()
    g.server1:update_box_cfg({ replication_synchro_timeout = 5 })
    g.server1:wait_for_election_leader()
end

g.before_test('test_session_on_commit', function()
    g.server1:exec(function()
        box.schema.user.create('eve')
        box.schema.user.grant('eve', 'write', 'space', 'test')
    end)
end)

g.test_session_on_commit = function(g)
    g.server1:exec(function()
        box.session.su('eve', function()
            t.assert_equals(box.session.effective_user(), 'eve')
            local id, user

            box.begin()
            box.space.test:upsert({1}, {{'=', 1, 1}})
            box.on_commit(function()
                id = box.session.id()
                user = box.session.effective_user()
            end)
            box.commit()

            t.assert_equals(id, box.session.id())
            t.assert_equals(user, box.session.effective_user())
        end)
    end)
end

g.after_test('test_session_on_commit', function()
    g.server1:exec(function()
        box.schema.user.drop('eve')
    end)
end)
