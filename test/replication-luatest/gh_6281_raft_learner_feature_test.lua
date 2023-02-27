local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-7512')

g.before_all(function(g)
    g.cluster = cluster:new{}
    g.box_cfg = {
        -- log_level = 7,
        election_mode = 'off',
        replication_timeout = 0.1,
        replication_synchro_quorum = 'N/2 + 1',
    }

    -- Qnly one instance on start: quorum = 1
    g.node1 = g.cluster:build_and_add_server{
        alias = 'node1',
        box_cfg = g.box_cfg,
    }

    g.cluster:start()
    g.node1:exec(function()
        box.ctl.promote()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('primary')

        -- Make relay on the first instance hang in case of reconfiguration
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', true)
    end)
end)

g.after_all(function(g)
    g.cluster:drop()
end)

g.test_sync_request_hang = function(g)
    local replication = {
        server.build_listen_uri('node1'),
        server.build_listen_uri('node2'),
    }

    g.box_cfg.replication = replication
    g.box_cfg.replication_connect_quorum = 2

    -- Add second instance in order to make synchro quorum = 2
    g.node2 = server:new({alias = 'node2', box_cfg = g.box_cfg})
    g.cluster:add_server(g.node2)

    -- Such a dirty workaround
    -- We cannot start server and configure it after
    -- it with exec as UUID mismatch will occur.
    --
    -- Async as box won't exit until fully connected to master
    require('os').setenv('TARANTOOL_RUN_BEFORE_BOX_CFG', '\
           require("fiber").create(function() \
                box.cfg(box_cfg()) \
           end)')
    g.node2:start({wait_until_ready = false})

    g.node1:exec(function(replication)
        require('fiber').create(function()
            box.cfg({replication = replication})
        end)
    end, {replication})

    t.helpers.retrying({timeout = 20}, function()
        if not g.node1:grep_log('replica join is delayed') then
            error("Initial join is not finished yet")
        end
    end)

    g.node1:exec(function()
        box.space.test:insert({1, 1})
    end)
end
