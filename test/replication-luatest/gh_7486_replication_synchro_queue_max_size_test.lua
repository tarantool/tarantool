local t = require('luatest')
local server = require('luatest.server')

local g = t.group('replication_synchro_queue_max_size')
--
-- gh-7486: introduce `replication_synchro_queue_max_size`.
--
local wait_timeout = 10

local function server_wait_synchro_queue_len_is_equal(server, expected)
    server:exec(function(expected, wait_timeout)
        t.helpers.retrying({timeout = wait_timeout}, function(expected)
            t.assert_equals(box.info.synchro.queue.len, expected)
        end, expected)
    end, {expected, wait_timeout})
end

g.before_all(function(cg)
    cg.box_cfg = {
        replication_synchro_queue_max_size = 1,
        replication_synchro_quorum = 1,
        replication_synchro_timeout = 100000,
        replication_timeout = 0.1,
        election_fencing_mode = 'off',
    }
    cg.master = server:new({
        alias = 'master',
        box_cfg = cg.box_cfg,
    })
    cg.master:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.master:drop()
end)

g.after_each(function(cg)
    cg.master:exec(function()
        box.space.test:truncate()
    end)
end)

g.test_master_synchro_queue_limited = function(cg)
    cg.master:exec(function()
        box.cfg{ replication_synchro_quorum = 2, }
        rawset(_G, "f", require('fiber')
            .new(box.space.test.insert, box.space.test, {1}))
        _G.f:set_joinable(true)
    end)
    server_wait_synchro_queue_len_is_equal(cg.master, 1)
    cg.master:exec(function()
        t.assert_error_msg_content_equals(
            'The synchronous transaction queue is full', function()
                    box.atomic({wait = 'none'}, function()
                        box.space.test:insert{2}
                    end)
                end)
        box.cfg{ replication_synchro_quorum = 1, }
        local ok, _ = _G.f:join()
        t.assert(ok)
    end, {wait_timeout})
end

g.test_max_size_update_dynamically = function(cg)
    cg.master:exec(function()
        box.cfg{
            replication_synchro_queue_max_size = 0,
            replication_synchro_quorum = 2,
        }
        rawset(_G, 'fibers_storage', {})
        rawset(_G, 'num_fibers', 3)
        for i = 1, _G.num_fibers do
            _G.fibers_storage[i] = require('fiber')
                .new(box.space.test.insert, box.space.test, {i})
            _G.fibers_storage[i]:set_joinable(true)
        end
    end)
    server_wait_synchro_queue_len_is_equal(cg.master, 3)
    cg.master:exec(function()
        box.cfg{ replication_synchro_queue_max_size = 1, }
        t.assert_equals(box.info.synchro.queue.len, 3)
        t.assert_error_msg_content_equals(
            'The synchronous transaction queue is full', function()
                    box.atomic({wait = 'none'}, function()
                        box.space.test:insert{0}
                    end)
                end)
        box.cfg{ replication_synchro_quorum = 1, }
        for i = 1, _G.num_fibers do
            local ok, _ = _G.fibers_storage[i]:join()
            t.assert(ok)
        end
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.space.test:insert{0}
    end)
end

g.test_recovery_with_small_max_size = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.master:exec(function(wait_timeout)
        box.cfg{
            replication_synchro_queue_max_size = 0,
            replication_synchro_quorum = 2,
        }
        local lsn = box.info.lsn + 1000
        for key = 1, 1000 do
            require('fiber').create(function()
                box.space.test:insert({key})
            end)
        end
        t.helpers.retrying({timeout = wait_timeout},
            function() t.assert(box.info.lsn >= lsn) end)
    end, {wait_timeout})
    server_wait_synchro_queue_len_is_equal(cg.master, 1000)
    local box_cfg = table.copy(cg.box_cfg)
    box_cfg.replication_synchro_queue_max_size = 1
    box_cfg.replication_synchro_timeout = 0.001
    cg.master:restart({
        box_cfg = box_cfg,
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG =
                "box.error.injection.set('ERRINJ_WAL_DELAY', true)"
        }
    })
    server_wait_synchro_queue_len_is_equal(cg.master, 1000)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.ctl.promote()
        t.assert_equals(box.space.test:len(), 1000)
        box.cfg{replication_synchro_timeout = 100000}
    end)
end

g.test_size_is_updated_correctly_after_commit = function(cg)
    cg.master:exec(function()
        box.space.test:insert{1}
        box.space.test:insert{2}
    end)
end

g.test_size_is_updated_correctly_after_rollback = function(cg)
    cg.master:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.001,
        }
        t.assert_error_msg_content_equals(
            'Quorum collection for a synchronous transaction is timed out',
            box.space.test.insert, box.space.test, {1})
        box.cfg{
            replication_synchro_quorum = 1,
            replication_synchro_timeout = 100000,
        }
        box.space.test:insert{2}
    end)
end
