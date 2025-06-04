local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            replication_synchro_queue_max_size = 1000,
            replication_synchro_timeout = 1000,
            election_mode = 'manual',
        }
    })
    cg.server:start()
    cg.server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')

        rawset(_G, 'test_timeout', 60)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
    if cg.replica then
        cg.replica:drop()
    end
end)

g.test_cascading_rollback_while_waiting_for_limbo_space = function(cg)
    cg.server:exec(function()
        local log = require('log')
        local fiber = require('fiber')
        local data = string.rep('a', 1000)
        local s = box.space.test

        rawset(_G, 'test_results', {})
        local function make_txn_fiber(id)
            return fiber.create(function()
                fiber.self():set_joinable(true)
                fiber.self():name(('worker-%s'):format(id))

                log.info('---------------------------- start')
                box.begin()
                box.on_rollback(function()
                    log.info('---------------------------- rollback')
                    table.insert(_G.test_results, ('rollback %s'):format(id))
                end)
                box.on_commit(function()
                    log.info('---------------------------- commit')
                    table.insert(_G.test_results, ('commit %s'):format(id))
                end)
                s:replace{id, data}
                log.info('---------------------------- start committing')
                box.commit()
            end)
        end

        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        log.info('---------------------------- start workers')
        rawset(_G, 'test_f1', make_txn_fiber(1))
        rawset(_G, 'test_f2', make_txn_fiber(2))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        log.info('---------------------------- workers are started')
    end)
    cg.replica = server:new({
        box_cfg = {
            replication = cg.server.net_box_uri,
            replication_timeout = 0.1,
            replication_anon = true,
            read_only = true,
        }
    })
    cg.replica:start({wait_until_ready = false})
    cg.server:exec(function()
        local log = require('log')
        log.info('---------------------------- waiting for anon replica to connect')
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert_gt(box.info.replication_anon.count, 0)
        end)
        log.info('---------------------------- waiting the first fiber to end')
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            if next(_G.test_results) then
                return
            end
            log.info('---------------------------- status = %s', _G.test_f1:status())
            log.info('---------------------------- allow next WAL write')
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.helpers.retrying({timeout = _G.test_timeout}, function()
                t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            end)
            t.assert_not('retry')
        end)
        log.info('---------------------------- join the first fiber')
        t.assert((_G.test_f1:join()))

        log.info('---------------------------- fail the second fiber')
        box.error.injection.set('ERRINJ_WAL_ROTATE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert_not((_G.test_f2:join()))
        box.error.injection.set('ERRINJ_WAL_ROTATE', false)

        log.info('---------------------------- check results')
        t.assert(box.space.test:get{1})
        t.assert_not(box.space.test:get{2})
    end)
    cg.replica:wait_until_ready()
    cg.replica:exec(function()
        t.assert(box.space.test:get{1})
        t.assert_not(box.space.test:get{2})
    end)
end
