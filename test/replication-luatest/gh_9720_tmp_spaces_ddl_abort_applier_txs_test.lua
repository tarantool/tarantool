local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix({memtx_use_mvcc = {false, true}}))

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        memtx_use_mvcc_engine = cg.params.memtx_use_mvcc,
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    box_cfg.read_only = true
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.server1:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('p')
        box.begin()
        for i = 1, 1000 do
            s:replace{i, i}
        end
        box.commit()
    end)
    cg.server2:exec(function()
        box.cfg{read_only = false}
    end)
    cg.replica_set:wait_for_fullmesh()
    cg.server1:wait_for_downstream_to(cg.server2)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test that a DDL transaction on fully temporary spaces does not abort a remote
-- transaction.
g.test_fully_temporary_ddl_does_not_abort_remote_tx = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.server2:exec(function()
        box.error.injection.set('ERRINJ_CHECK_FORMAT_DELAY_COUNTDOWN', 0)
    end)
    cg.server1:exec(function()
        box.space.s:format{'i', 'unsigned'}
    end)
    cg.server2:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert(box.error.injection.get('ERRINJ_CHECK_FORMAT_DELAY', true))
        end)
        box.schema.space.create('tmp', {type = 'temporary'})
        box.error.injection.set('ERRINJ_CHECK_FORMAT_DELAY', false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        local msg = "Transaction has been aborted by conflict"
        t.assert_not_equals(box.info.replication[1].upstream.message, msg)
        t.assert_not_equals(box.space.s:format(), {})
    end)
end

-- Test that a DDL transaction on fully temporary spaces still aborts a not
-- fully remote transaction.
g.test_fully_temporary_ddl_aborts_not_fully_remote_tx = function(cg)
    t.skip_if(not cg.params.memtx_use_mvcc)

    cg.server2:exec(function()
        local tmp = box.schema.space.create('tmp-replace', {type = 'temporary'})
        tmp:create_index('p')

        box.space.s:on_replace(function()
            tmp:replace{0}
            rawset(_G, 'executed_on_replace', true)
            require('fiber').sleep(60)
        end)
    end)
    cg.server1:exec(function()
        box.space.s:replace{0}
    end)
    cg.server2:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(_G.executed_on_replace)
        end)
        box.schema.space.create('tmp', {type = 'temporary'})
    end)
    local msg = "Transaction committing DDL %(id=%d+%) has aborted " ..
                "another TX %(id=%d+%)"
    t.assert(cg.server2:grep_log(msg))
    cg.server2:exec(function()
        t.assert_equals(box.space.s:get{0}, nil)
    end)
end

-- Test that a DDL transaction on fully temporary spaces does not abort a
-- fully remote transaction that rollbacked to a savepoint without local
-- changes.
g.test_fully_temporary_ddl_does_not_abort_fully_remote_tx_after_rb_to_svp =
function(cg)
    t.skip_if(not cg.params.memtx_use_mvcc)

    cg.server2:exec(function()
        local tmp = box.schema.space.create('tmp-replace', {type = 'temporary'})
        tmp:create_index('p')

        box.space.s:on_replace(function()
            local svp = box.savepoint()
            tmp:replace{0}
            box.rollback_to_savepoint(svp)
            rawset(_G, 'executed_on_replace', true)
            t.helpers.retrying({timeout = 120}, function()
                t.assert(_G.can_leave_on_replace)
            end)
        end)
    end)
    cg.server1:exec(function()
        box.space.s:replace{0}
    end)
    cg.server2:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert(_G.executed_on_replace)
        end)
        box.schema.space.create('tmp', {type = 'temporary'})
        rawset(_G, 'can_leave_on_replace', true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.space.s:get{0}, nil)
    end)
end

-- Test that a remote DDL transaction does not abort a transaction on fully
-- temporary spaces.
g.test_fully_remote_ddl_does_not_abort_fully_temporary_tx = function(cg)
    t.tarantool.skip_if_not_debug()

    local fid = cg.server2:exec(function()
        local tmp = box.schema.space.create('tmp', {type = 'temporary'})
        tmp:create_index('p')
        box.begin()
        for i = 1, 1000 do
            tmp:replace{i, i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_CHECK_FORMAT_DELAY', true)
        local f = require('fiber').new(function()
            tmp:format{'i', 'unsigned'}
        end)
        f:set_joinable(true)
        return f:id()
    end)
    cg.server1:exec(function()
        box.schema.space.create('ss')
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function(fid)
        box.error.injection.set('ERRINJ_CHECK_FORMAT_DELAY', false)
        local ok = require('fiber').find(fid):join()
        t.assert(ok)
        t.assert_not_equals(box.space.tmp:format(), {})
    end, {fid})
end

-- Test that a remote DDL transaction still aborts a not fully temporary
-- transaction.
g.test_fully_remote_tx_ddl_aborts_not_fully_temporary_tx = function(cg)
    t.skip_if(not cg.params.memtx_use_mvcc)

    local fid = cg.server2:exec(function()
        local fiber = require('fiber')

        local tmp = box.schema.space.create('tmp-replace', {type = 'temporary'})
        tmp:create_index('p')

        rawset(_G, 'downstream_cond', fiber.cond())
        local f = fiber.new(function()
            box.begin()
            tmp:replace{0}
            box.space.s:replace{0}
            _G.downstream_cond:wait()
            box.commit()
        end)
        f:set_joinable(true)
        return f:id()
    end)
    cg.server1:exec(function()
        box.schema.space.create('ss')
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function(fid)
        _G.downstream_cond:signal()
        local ok, err = require('fiber').find(fid):join()
        t.assert_not(ok)
        t.assert_equals(err.message, "Transaction has been aborted by conflict")
    end, {fid})
end

-- Test that a remote DDL transaction does not abort a fully temporary
-- transaction that rolled back to a savepoint without non-temporary changes.
g.test_fully_remote_tx_ddl_does_not_abort_fully_temporary_tx_after_rb_to_svp =
function(cg)
    t.skip_if(not cg.params.memtx_use_mvcc)

    local fid = cg.server2:exec(function()
        local fiber = require('fiber')

        local tmp = box.schema.space.create('tmp-replace', {type = 'temporary'})
        tmp:create_index('p')

        rawset(_G, 'downstream_cond', fiber.cond())
        local f = fiber.new(function()
            box.begin()
            tmp:replace{0}
            local svp = box.savepoint()
            box.space.s:replace{0}
            box.rollback_to_savepoint(svp)
            _G.downstream_cond:wait()
            box.commit()
        end)
        f:set_joinable(true)
        return f:id()
    end)
    cg.server1:exec(function()
        box.schema.space.create('ss')
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function(fid)
        _G.downstream_cond:signal()
        local ok = require('fiber').find(fid):join()
        t.assert(ok)
    end, {fid})
end

-- Test that a fully remote DDL transaction still aborts a data-temporary
-- transaction.
g.test_fully_remote_tx_ddl_aborts_data_temporary_tx = function(cg)
    t.skip_if(not cg.params.memtx_use_mvcc)

    local fid = cg.server2:exec(function()
        local fiber = require('fiber')

        local tmp = box.schema.space.create('data-tmp',
                                            {type = 'data-temporary'})
        tmp:create_index('p')

        rawset(_G, 'downstream_cond', fiber.cond())
        local f = fiber.new(function()
            box.begin()
            tmp:replace{0}
            _G.downstream_cond:wait()
            box.commit()
        end)
        f:set_joinable(true)
        return f:id()
    end)
    cg.server1:exec(function()
        box.schema.space.create('ss')
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function(fid)
        _G.downstream_cond:signal()
        local ok, err = require('fiber').find(fid):join()
        t.assert_not(ok)
        t.assert_equals(err.message, "Transaction has been aborted by conflict")
    end, {fid})
end
