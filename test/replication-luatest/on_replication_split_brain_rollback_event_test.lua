local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 120,
        replication_synchro_quorum = 3,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = cg.box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        rawset(_G, 'trigger', require('trigger'))
        rawset(_G, 'trigger_ok', false)
        local event = 'box.ctl.on_replication_split_brain_rollback'
        local body = 'function() rawset(_G, "trigger_called", true) end'
        box.schema.func.create('f', {body = body, trigger = event})
        box.schema.space.create('a', {is_sync = false}):create_index('p')
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.schema.space.create('l', {is_local = true}):create_index('p')
        box.ctl.promote()
    end)
    cg.master:wait_for_downstream_to(cg.replica)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

local function partition_server(s)
    s:exec(function()
        box.cfg{replication = ''}
    end)
end

local function cause_split_brain(master, replica)
    partition_server(replica)
    master:exec(function()
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    replica:exec(function()
        box.ctl.promote()
    end)
end

local function check_promote_from_partitioned_server(partitioned, master,
                                                     replication)
    partitioned:exec(function()
        box.ctl.promote()
    end)
    master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert(_G.trigger_ok)
    end)
    partitioned:exec(function(replication)
        box.cfg{replication = replication}
    end, {replication})
    master:exec(function()
        box.ctl.promote()
    end)
    master:wait_for_downstream_to(partitioned)
end

local function test_stmt(cg, stmt)
    partition_server(cg.replica)
    cg.master:exec(function(stmt)
        box.atomic({wait = 'submit'}, function()
            box.space.s[stmt.request](box.space.s, stmt.arg, stmt.ops)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', _G.generate_trigger_test_case(stmt))
    end, {stmt})
    check_promote_from_partitioned_server(cg.replica, cg.master,
                                          cg.box_cfg.replication)
end

local function test_several_stmts(cg)
    partition_server(cg.replica)
    cg.master:exec(function()
        box.atomic({wait = 'submit'}, function()
            box.space.s:replace{0}
            box.space.s:replace{0}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        local event = 'box.ctl.on_replication_split_brain_rollback'
        local stmt = {request = 'replace', arg = {0}, ops = nil,
                      old_tuple = nil, new_tuple = {0}}
        _G.trigger.set(event, 't', _G.generate_trigger_test_case(stmt, 2))
    end)
    check_promote_from_partitioned_server(cg.replica, cg.master,
                                          cg.box_cfg.replication)
end

g.before_test('test_different_stmts', function(cg)
    cg.master:exec(function()
        rawset(_G, 'generate_trigger_test_case',
            function(stmt, n_stmts)
                return function(it)
                    n_stmts = n_stmts ~= nil and n_stmts or 1
                    local counter = 1
                    for obj in it() do
                        _G.trigger_ok = pcall(function()
                            local meta = obj.meta
                            local rid = meta.replica_id
                            local lsn = meta.lsn
                            local term = meta.term
                            local sid = obj.statement.space_id
                            local op_type = obj.statement.op_type
                            local arg
                            if stmt.request == "replace" or
                               stmt.request == "insert" or
                               stmt.request == "upsert" then
                                arg = obj.statement.tuple
                                t.assert_equals(obj.statement.key, nil)
                            else
                                arg = obj.statement.key
                            end
                            local ops
                            if stmt.request == "update" then
                                ops = obj.statement.tuple
                                t.assert_equals(obj.statement.ops, nil)
                            else
                                ops = obj.statement.ops
                            end
                            local old_tuple = obj.statement.old_tuple
                            local new_tuple = obj.statement.new_tuple

                            t.assert_equals(term, box.info.synchro.queue.term)
                            t.assert_equals(rid, box.info.id)
                            if counter == n_stmts then
                               t.assert_equals(lsn, box.info.lsn)
                            end
                            t.assert_equals(sid, box.space.s.id)
                            t.assert_equals(op_type, stmt.request:upper())
                            t.assert_equals(arg, stmt.arg)
                            if stmt.ops ~= nil then
                                t.assert_equals(ops, stmt.ops)
                            end
                            if counter == 1 then
                                t.assert_equals(old_tuple, stmt.old_tuple)
                            else
                                t.assert_equals(old_tuple, arg)
                            end
                            t.assert_equals(new_tuple, stmt.new_tuple)
                            counter = counter + 1
                        end)
                    end
                    t.assert_equals(n_stmts, counter - 1)
                end
            end)
        box.cfg{replication_synchro_quorum = ''}
        -- Add a tuple to test `update` and `upsert` requests.
        box.space.s:replace{777, 0}
        box.cfg{replication_synchro_quorum = 3}
    end)
end)

-- Test that the event iterator returns correct information for different
-- transaction statements.
g.test_different_stmts = function(cg)
    local stmts = {
        {request = 'replace', arg = {0}, ops = nil,
         old_tuple = nil, new_tuple = {0}},
        {request = 'replace', arg = {777, 1}, ops = nil,
         old_tuple = {777, 0}, new_tuple = {777, 1}},
        {request = 'insert', arg = {0}, ops = nil,
         old_tuple = nil, new_tuple = {0}},
        {request = 'delete', arg = {0}, ops = nil,
         old_tuple = nil, new_tuple = nil},
        {request = 'delete', arg = {777}, ops = nil,
         old_tuple = {777, 0}, new_tuple = nil},
        {request = 'update', arg = {777}, ops = {{'=', 2, 1}},
         old_tuple = {777, 0}, new_tuple = {777, 1}},
        {request = 'update', arg = {777}, ops = {{'=', 2, 1}, {'=', 3, 1}},
         old_tuple = {777, 0}, new_tuple = {777, 1, 1}},
        {request = 'upsert', arg = {777}, ops = {{'=', 2, 1}},
         old_tuple = {777, 0}, new_tuple = {777, 1}},
        {request = 'upsert', arg = {0}, ops = {{'=', 2, 1}},
         old_tuple = nil, new_tuple = {0}},
    }
    for _, stmt in ipairs(stmts) do
       test_stmt(cg, stmt)
    end

    test_several_stmts(cg)
end

-- Test that the trigger does not fire during recovery.
g.test_recovery = function(cg)
    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert(_G.trigger_called)
    end)
    cg.master:restart()
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_not(rawget(_G, 'trigger_called'))
    end)
end

-- Test that the trigger is fired for an asynchronously committed asynchronous
-- transaction.
g.test_async_committed_async_tx_in_limbo = function(cg)
    partition_server(cg.replica)
    cg.master:exec(function()
        require('fiber').create(function()
            box.space.s:replace{0}
        end)
        box.atomic({wait = 'submit'}, function() box.space.a:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 2)
    end)
    cg.replica:exec(function()
        box.ctl.promote()
    end)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert(_G.trigger_called)
    end)
end

-- Test that the trigger does not fire for synchronously committed synchronous
-- transactions.
g.test_sync_committed_sync_tx = function(cg)
    partition_server(cg.replica)
    cg.master:exec(function()
        require('fiber').create(function()
            box.space.s:replace{0}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    cg.replica:exec(function()
        box.ctl.promote()
    end)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_not(rawget(_G, 'trigger_called'))
    end)
end

g.before_test('test_replica', function(cg)
    local test_replica_uri =
        server.build_listen_uri('test_replica', cg.replica_set.id)
    table.insert(cg.box_cfg.replication, test_replica_uri)
    cg.test_replica = cg.replica_set:build_and_add_server{
        alias = 'test_replica',
        box_cfg = cg.box_cfg,
    }
    cg.test_replica:start()
    cg.master:update_box_cfg{replication = cg.box_cfg.replication}
    cg.replica:update_box_cfg{replication = cg.box_cfg.replication}
    cg.replica_set:wait_for_fullmesh()
end)

-- Test that the trigger does not fire on replicas.
g.test_replica = function(cg)
    partition_server(cg.replica)
    cg.master:exec(function()
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    cg.test_replica:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end)
    cg.replica:exec(function()
        box.ctl.promote()
    end)
    cg.replica:wait_for_downstream_to(cg.test_replica)
    cg.test_replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_not(rawget(_G, 'trigger_called'))
    end)
end

g.after_test('test_replica', function(cg)
    cg.test_replica:drop()
    table.remove(cg.box_cfg.replication)
end)

g.before_test('test_trigger_failure', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            rawset(_G, 'trigger_called', true)
            box.space.l:replace{0}
            error('777')
        end)
    end)
end)

-- Test that the trigger failure causes split brain and rollback of transaction.
g.test_trigger_failure = function(cg)
    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({timeout = 120}, function()
            local upstream = box.info.replication[replica_id].upstream
            t.assert_equals(upstream.status, 'stopped')
            local msg = 'Split-Brain discovered: ' ..
                        'box.ctl.on_split_brain_rollback trigger failed'
            t.assert_equals(upstream.message, msg)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.l:get{0}, nil)
        t.assert(_G.trigger_called)
    end, {cg.replica:get_instance_id()})
end

g.before_test('test_non_fully_local_txns', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            local msg = "Can't modify data on a read-only instance"
            _G.trigger_ok = pcall(function()
                t.assert_error_msg_contains(msg, function()
                    box.space.a:replace{0}
                end)
            end)
        end)
    end)
end)

-- Test that non fully local transactions are forbidden during execution of the
-- event triggers.
g.test_non_fully_local_txns = function(cg)
    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert(_G.trigger_ok)
    end)
end

g.before_test('test_fully_local_txn', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            t.assert(box.is_in_txn())
            box.space.l:replace{0}
        end)
    end)
end)

-- Test that fully local transactions are allowed during execution of the event
-- triggers.
g.test_fully_local_txn = function(cg)
    partition_server(cg.replica)
    cg.master:exec(function()
        -- Test that the trigger is called multiple times and can update fully
        -- local spaces throughout the whole time.
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 2)
    end)
    cg.replica:exec(function()
        box.ctl.promote()
    end)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.l:get{0}, {0})
    end)
    cg.master:restart()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.l:get{0}, {0})
    end)
end

g.before_test('test_explicit_commit', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            box.space.l:replace{0}
            box.commit()
        end)
    end)
end)

-- Test that the explicit commit of the transaction is handled correctly.
g.test_explicit_commit = function(cg)
    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.l:get{0}, {0})
    end)
    cg.master:restart()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.l:get{0}, {0})
    end)
end

g.before_test('test_implicit_commit_failure', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            box.error.injection.set('ERRINJ_WAL_IO', true)
            box.space.l:replace{0}
            box.on_rollback(function()
                box.error.injection.set('ERRINJ_WAL_IO', false)
            end)
        end)
    end)
end)

-- Test that failure to implicitly commit a pending transaction after execution
-- of the event triggers finishes caused split brain.
g.test_implicit_commit_failure = function(cg)
    t.tarantool.skip_if_not_debug()

    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({timeout = 120}, function()
            local upstream = box.info.replication[replica_id].upstream
            t.assert_equals(upstream.status, 'stopped')
            local msg = 'Failed to write to disk'
            t.assert_equals(upstream.message, msg)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end, {cg.replica:get_instance_id()})
end

g.before_test('test_second_tx_failure', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            box.commit()
            _G.trigger_ok = pcall(function() box.space.l:replace{0} end)
        end)
    end)
end)

-- Test that the second transaction started in the trigger will fail, since it
-- won't have the `TXN_FORCE_ASYNC` flag set.
g.test_second_tx_failure = function(cg)
    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_not(_G.trigger_ok)
    end)
end

g.before_test('test_second_tx_implicit_commit_failure', function(cg)
    cg.master:exec(function()
        local event = 'box.ctl.on_replication_split_brain_rollback'
        _G.trigger.set(event, 't', function()
            box.commit()
            box.begin()
            box.space.l:replace{0}
        end)
    end)
end)

-- Test that the implicit commit of the second transaction started in the
-- trigger will fail, since it won't have the `TXN_FORCE_ASYNC` flag set.
g.test_second_tx_implicit_commit_failure = function(cg)
    cause_split_brain(cg.master, cg.replica)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({timeout = 120}, function()
            local upstream = box.info.replication[replica_id].upstream
            t.assert_equals(upstream.status, 'loading')
            local msg = 'A rollback for a synchronous transaction is received'
            t.assert_equals(upstream.message, msg)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end, {cg.replica:get_instance_id()})
end
