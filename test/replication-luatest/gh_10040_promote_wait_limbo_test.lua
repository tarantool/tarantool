local t = require('luatest')
local g = t.group('gh-10040-promote-quorum')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

g.before_all(function(cg)
    cg.replica_set = replica_set:new({})
    local master_uri = server.build_listen_uri('master', cg.replica_set.id)
    local replica_uri = server.build_listen_uri('replica', cg.replica_set.id)
    local config = {
        election_mode = 'manual',
        replication = {master_uri, replica_uri},
        replication_synchro_timeout = 30
    }
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = config
    })
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = config
    })
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        t.assert(pcall(box.ctl.promote))
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk', {if_not_exists = true})
    end)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_promote_waits_for_quorum = function(cg)
    cg.master:exec(function()
        box.cfg{replication_synchro_quorum = 3}
        box.atomic({wait = 'submit'}, function()
            box.space.test:insert{1}
        end)
    end)
    cg.replica:exec(function()
        local fiber = require('fiber')
        box.cfg{replication_synchro_quorum = 3}
        t.helpers.retrying({timeout = 5}, function()
            t.assert_gt(box.info.synchro.queue.len, 0)
        end)
        local prom_fiber = fiber.new(function()
            return pcall(box.ctl.promote)
        end)
        prom_fiber:set_joinable(true)
        t.helpers.retrying({timeout = 5}, function()
            t.assert_equals(box.info.election.state, 'leader')
        end)
        local is_finished, _ = prom_fiber:join(0.01)
        t.assert_not(is_finished)
        box.cfg({replication_synchro_quorum = 2})
        local _, _, err = prom_fiber:join()
        t.assert_not(err)
        t.assert_not(box.info.ro)
    end)
end
