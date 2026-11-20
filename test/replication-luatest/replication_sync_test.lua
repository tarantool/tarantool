local t = require('luatest')
local server = require('luatest.server')
local g = t.group('replication_sync')

g.before_all(function(cg)
    cg.master = server:new({alias = 'master', box_cfg = {
        replication_timeout = 0.1,
    }})
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('s'):create_index('pk')
    end)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        read_only = true,
        replication_timeout = 0.1,
    }})
    cg.replica:start()
    cg.anon = server:new({alias = 'anon', box_cfg = {
        replication = cg.master.net_box_uri,
        replication_anon = true,
        read_only = true,
        replication_timeout = 0.1,
    }})
    cg.anon:start()
    cg.master:exec(function()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end)

g.after_all(function(cg)
    cg.anon:drop()
    cg.replica:drop()
    cg.master:drop()
end)

g.test_arguments_and_local_component = function(cg)
    cg.master:exec(function()
        for _, opts in ipairs({1, true, {unknown = true}, {timeout = -1},
                               {timeout = 0/0}, {timeout = '1'},
                               {include_anon = 1}}) do
            t.assert_error(box.ctl.replication_sync, opts)
        end
        t.assert_error(box.ctl.replication_sync, {}, {})
        local s = box.schema.space.create('local_space', {is_local = true})
        s:create_index('pk')
        box.ctl.replication_sync{include_anon = true, timeout = 120}
        s:insert{1}
        t.assert_equals({box.ctl.replication_sync{timeout = 0}}, {})
        s:drop()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

g.test_ack_timeout_and_cancellation = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    cg.master:exec(function()
        local fiber = require('fiber')
        box.space.s:insert{1}
        t.assert_error_covers({type = 'TimedOut'}, box.ctl.replication_sync,
                              {timeout = 0})
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync()
        end)
        f:cancel()
        local ok, err = f:join()
        t.assert_not(ok)
        t.assert_equals(err.type, 'FiberIsCancelled')
        rawset(_G, 'waiter', fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync{timeout = 120}
        end))
    end)
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.master:exec(function()
        t.assert(_G.waiter:join())
        _G.waiter = nil
    end)
    cg.replica:exec(function() t.assert_equals(box.space.s:get{1}, {1}) end)
    cg.master:exec(function()
        box.space.s:truncate()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

g.test_disconnected_target_is_retained = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    cg.master:exec(function()
        local fiber = require('fiber')
        box.space.s:insert{1}
        rawset(_G, 'waiter', fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync{timeout = 120}
        end))
    end)
    -- Stop without waiting for the intentionally stalled WAL write.
    cg.replica.process:kill('KILL')
    cg.replica:stop()
    cg.master:exec(function()
        t.assert_not_equals(_G.waiter:status(), 'dead')
        t.assert_error_covers({type = 'TimedOut'}, box.ctl.replication_sync,
                              {timeout = 0})
    end)
    cg.replica:start()
    cg.master:exec(function()
        t.assert(_G.waiter:join())
        _G.waiter = nil
        box.space.s:truncate()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

g.test_captured_members_and_vclock = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    cg.master:exec(function()
        local fiber = require('fiber')
        box.space.s:insert{1}
        rawset(_G, 'waiter', fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync{timeout = 120}
        end))
    end)
    cg.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.master:exec(function()
        -- A newly registered unavailable replica cannot extend an older wait.
        box.space._cluster:insert{3, require('uuid').str()}
        box.space.s:insert{2}
        t.assert(_G.waiter:join())
        _G.waiter = nil
        t.assert_error_covers({type = 'TimedOut'}, box.ctl.replication_sync,
                              {timeout = 0})
        box.space._cluster:delete{3}
        box.space.s:truncate()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

g.test_removed_target_is_retained = function(cg)
    cg.master:exec(function()
        box.space._cluster:insert{3, require('uuid').str()}
        box.space.s:insert{1}
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function()
        local fiber = require('fiber')
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync{timeout = 0.01}
        end)
        box.space._cluster:delete{3}
        local ok, err = f:join()
        t.assert_not(ok)
        t.assert_equals(err.type, 'TimedOut')
        box.space.s:truncate()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

g.test_include_anonymous_replica = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.anon:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    cg.master:exec(function()
        local fiber = require('fiber')
        box.space.s:insert{1}
        -- Wait for the registered replica; the anonymous one is still stalled.
        box.ctl.replication_sync{timeout = 120}
        t.assert_equals({box.ctl.replication_sync{timeout = 0}}, {})
        t.assert_equals({box.ctl.replication_sync{
            include_anon = false,
            timeout = 0,
        }}, {})
        t.assert_error_covers({type = 'TimedOut'}, box.ctl.replication_sync,
                              {include_anon = true, timeout = 0})
        rawset(_G, 'waiter', fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync{include_anon = true, timeout = 120}
        end))
    end)
    cg.anon:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.master:exec(function()
        t.assert(_G.waiter:join())
        _G.waiter = nil
    end)
    cg.anon:exec(function() t.assert_equals(box.space.s:get{1}, {1}) end)
    cg.master:exec(function()
        box.space.s:truncate()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

g.test_disconnected_anonymous_replica_is_retained = function(cg)
    t.tarantool.skip_if_not_debug()
    local uuid = cg.anon:get_instance_uuid()
    cg.anon:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    cg.master:exec(function()
        local fiber = require('fiber')
        box.space.s:insert{1}
        box.ctl.replication_sync{timeout = 120}
        rawset(_G, 'waiter', fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.replication_sync{include_anon = true, timeout = 120}
        end))
    end)
    -- Stop without waiting for the intentionally stalled WAL write.
    cg.anon.process:kill('KILL')
    cg.anon:stop()
    cg.master:exec(function(uuid)
        t.helpers.retrying({timeout = 120}, function()
            local replica = box.info.replication_anon()[uuid]
            t.assert_equals(replica.downstream.status, 'stopped')
        end)
        t.assert_not_equals(_G.waiter:status(), 'dead')
        -- A new call does not include the already disconnected anonymous node.
        box.ctl.replication_sync{include_anon = true, timeout = 0}
    end, {uuid})
    cg.anon:start()
    cg.master:exec(function()
        t.assert(_G.waiter:join())
        _G.waiter = nil
        box.space.s:truncate()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end
