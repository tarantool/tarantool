local t = require('luatest')
local server = require('luatest.server')
local uuid = require('uuid')

local g = t.group('join')

g.before_all(function(lg)
    lg.master = server:new({
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
        },
    })
    lg.master:start()
end)

g.after_all(function(lg)
    lg.master:drop()
end)

--
-- New replica (or one being re-bootstrapped) can legally see deletion of self
-- from _cluster space during join. It is allowed because the master could
-- delete the replica from _cluster before the replica tried to rejoin or join
-- first time via a previously existing UUID.
--
g.test_fetch_self_delete_during_final_join = function(lg)
    t.tarantool.skip_if_not_debug()
    lg.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:replace{1}
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
    end)
    local replica_uuid = uuid.str()
    local box_cfg = table.deepcopy(lg.master.box_cfg)
    box_cfg.replication = {lg.master.net_box_uri}
    box_cfg.instance_uuid = replica_uuid
    local replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    replica:start({wait_until_ready = false})
    local msg = ('joining replica %s'):format(replica_uuid):gsub('%-', '%%-')
    t.helpers.retrying({}, function()
        t.assert(lg.master:grep_log(msg))
    end)
    lg.master:exec(function(replica_uuid)
        local _cluster = box.space._cluster
        _cluster:replace{2, replica_uuid}
        _cluster:delete{2}
        _cluster:replace{2, replica_uuid}
        _cluster:delete{2}
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
    end, {replica_uuid})
    replica:wait_until_ready()
    local replica_id = replica:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
        return box.info.id
    end)
    replica:drop()
    lg.master:exec(function(replica_id)
        box.space.test:drop()
        box.space._cluster:delete{replica_id}
    end, {replica_id})
end

--
-- gh-10089: raft state was never sent during META_JOIN stage.
-- If replica managed to bump term right after subscribe, then
-- spit brain happened, as replica didn't have the actual raft
-- state until it's sent in response to SUBSCRIBE.
--
g.test_raft_state_during_meta_join = function(lg)
    t.tarantool.skip_if_not_debug()
    lg.master:update_box_cfg{election_mode = 'candidate'}
    lg.master:wait_for_election_leader()

    local box_cfg = table.deepcopy(lg.master.box_cfg)
    box_cfg.replication = {lg.master.net_box_uri}
    local replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG =
                "box.error.injection.set( \
                    'ERRINJ_APPLIER_SUBSCRIBE_DELAY', true)"
        },
    })

    replica:start()
    local ok, err = replica:exec(function()
        return pcall(box.ctl.promote)
    end)

    --
    -- Before the patch there's the split brain error:
    -- "Split-Brain discovered: got a PROMOTE/DEMOTE with an obsolete term"
    --
    t.assert_equals(err, nil)
    t.assert(ok)

    replica:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_SUBSCRIBE_DELAY', false)
    end)

    replica:drop()
    lg.master:update_box_cfg{election_mode = 'off'}
end
