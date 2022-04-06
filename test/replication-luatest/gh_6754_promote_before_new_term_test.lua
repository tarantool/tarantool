local luatest = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')
local g = luatest.group('gh-6754-promote-before-new-term')

g.before_all(function(g)
    g.cluster = cluster:new({})

    g.box_cfg = {
        read_only = true,
        election_mode = 'off',
        replication_synchro_quorum = 1,
        replication_timeout = 0.1,
        replication = {
            server.build_instance_uri('server_1'),
            server.build_instance_uri('server_2'),
        },
    }

    g.server_1 = g.cluster:build_and_add_server(
        {alias = 'server_1', box_cfg = g.box_cfg})

    g.box_cfg.read_only = false

    g.server_2 = g.cluster:build_and_add_server(
        {alias = 'server_2', box_cfg = g.box_cfg})
    g.cluster:start()
    g.cluster:wait_fullmesh()
end)

g.after_all(function(g)
    g.cluster:stop()
end)

g.test_promote_new_term_order = function(g)
    g.server_1:exec(function()
        box.error.injection.set('ERRINJ_RELAY_FROM_TX_DELAY', true)
        box.ctl.promote()
        box.cfg{read_only = false}
        box.schema.create_space('test'):create_index('pk')
        box.space.test:replace({1})
    end)
    luatest.helpers.retrying({}, function()
        g.server_2:exec(function()
            require('luatest').assert(box.space.test ~= nil)
            require('luatest').assert(#box.space.test:select() == 1)
        end)
    end)

    local election_term = g.server_2:election_term()
    local synchro_queue_term = g.server_2:synchro_queue_term()

    g.server_1:exec(function()
        box.error.injection.set('ERRINJ_RELAY_FROM_TX_DELAY', false)
    end)
    g.server_2:wait_election_term(synchro_queue_term)

    luatest.assert_le(synchro_queue_term, election_term,
        'new term always arrives before promote')
end
