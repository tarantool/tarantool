local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- Public API for foreign threads to execute work in TX thread.
--


local load_helper = function(server)
    server:exec(function()
        package.cpath = ('%s/test/app-luatest/lib/?.%s;%s'):format(
            os.getenv('BUILDDIR'), jit.os == 'OSX' and 'dylib' or 'so',
            package.path)
        local lib = box.lib.load('tx_user_fiber_pool')

        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'worker_start', lib:load('worker_start'))
        rawset(_G, 'worker_stop', lib:load('worker_stop'))
        rawset(_G, 'worker_echo', lib:load('worker_echo'))
        rawset(_G, 'worker_insert', lib:load('worker_insert'))
        rawset(_G, 'worker_flush', lib:load('worker_flush'))
        rawset(_G, 'tx_wait_key', lib:load('tx_wait_key'))
        rawset(_G, 'tx_set_max_size', lib:load('tx_set_max_size'))
        rawset(_G, 'tx_get_pending_count', lib:load('tx_get_pending_count'))
        rawset(_G, 'tx_pop_all', lib:load('tx_pop_all'))
        rawset(_G, 'tx_get_all', lib:load('tx_get_all'))
    end)
end

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    load_helper(cg.server)
end)

g.before_each(function(cg)
    cg.server:exec(function()
        _G.worker_start()
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        _G.worker_stop()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_basic = function(cg)
    cg.server:exec(function()
        for i = 1, 3 do
            _G.worker_echo(i)
        end
        _G.fiber.sleep(0.01)
        t.assert_equals(_G.tx_get_all(), {})

        _G.worker_flush()
        _G.tx_wait_key(3)
        t.assert_equals(_G.tx_pop_all(), {1, 2, 3})
    end)
end

g.test_pool_size = function(cg)
    cg.server:exec(function()
        t.assert_error_msg_contains(
            'size must be > 0', _G.fiber.tx_user_pool_size, -1)
        t.assert_error_msg_contains(
            'size must be > 0', _G.fiber.tx_user_pool_size, 0)
        t.assert_error_msg_contains(
            'size must be > 0', _G.fiber.tx_user_pool_size, 'crap')

        local old_size = _G.fiber.tx_user_pool_size()
        t.assert_equals(old_size, 768)
        _G.fiber.tx_user_pool_size(5)
        t.assert_equals(_G.fiber.tx_user_pool_size(), 5)

        _G.tx_set_max_size(3)
        local count = 57
        local expected = {}
        for i = 1, count do
            _G.worker_echo(i)
            table.insert(expected, i)
        end
        _G.worker_flush()
        _G.tx_wait_key(3)
        -- The first ones are waiting to be popped.
        t.assert_equals(_G.tx_get_all(), {1, 2, 3})
        -- The other ones are waiting for free space. All the pool fibers are
        -- taken now.
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(_G.tx_get_pending_count(), 5)
        end)
        -- Pushing and flushing more won't help.
        count = count + 1
        _G.worker_echo(count)
        table.insert(expected, count)
        _G.worker_flush()
        _G.fiber.sleep(0.01)
        t.assert_equals(_G.tx_get_all(), {1, 2, 3})
        t.assert_equals(_G.tx_get_pending_count(), 5)

        -- Let them to go through finally.
        _G.tx_set_max_size(1000)
        _G.tx_wait_key(count)
        t.assert_equals(_G.tx_pop_all(), expected)

        _G.fiber.tx_user_pool_size(old_size)
    end)
end

g.after_test('test_gh_11267_box_operations', function(cg)
    cg.server:exec(function()
        _G.worker_stop()
    end)
    -- Restart server as the test may influence the others due
    -- to spawning many fibers.
    cg.server:restart()
    load_helper(cg.server)
end)

g.test_gh_11267_box_operations = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10000 do
            _G.worker_insert(s.id, {i})
        end
        _G.worker_flush()
        t.helpers.retrying({}, function()
            t.assert_equals(s:count(), 10000)
        end)
    end)
end
