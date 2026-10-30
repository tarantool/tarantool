local t = require('luatest')
local replica_set = require('luatest.replica_set')

local g = t.group(nil, t.helpers.matrix({engine = {'memtx', 'vinyl'}}))

g.before_all(function(cg)
    cg.replica_set = replica_set:new()
    cg.master = cg.replica_set:build_and_add_server(
            {
                alias = 'master',
                box_cfg = {
                    replication_timeout = 0.1,
                },
            })
    cg.replica = cg.replica_set:build_and_add_server(
            {
                alias = 'replica',
                box_cfg = {
                    replication = cg.master.net_box_uri,
                    replication_timeout = 0.1,
                    memtx_use_mvcc_engine = true,
                    txn_isolation = 'read-confirmed',
                }
            })
    cg.replica_set:start()
    cg.master:exec(function(engine)
        local s = box.schema.space.create('s', {engine = engine})
        s:create_index('pk')
        s:insert{0}
    end, {cg.params.engine})
    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.replica:exec(function()
        box.cfg{replication = ""}
    end)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

-- Checks that replication stream does not get transaction conflict errors.
g.test_replication_stream_transaction_conflict_errors = function(cg)
    cg.replica:exec(function()
        t.assert_equals(box.space.s:select{}, {{0}})
    end)
    cg.master:exec(function()
        box.space.s:delete{0}
        -- Delete statement sees {0} deleted by another prepared statement and
        -- must not conflict with it.
        box.space.s:delete{0}
    end)
    cg.replica:exec(function(master_uri)
        box.cfg{replication = master_uri}
    end, {cg.master.net_box_uri})
    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.replica:exec(function()
        t.assert_equals(box.space.s:select{}, {})
    end)
end
