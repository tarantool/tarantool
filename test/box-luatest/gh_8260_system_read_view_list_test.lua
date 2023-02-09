local server = require('luatest.server')
local t = require('luatest')

local g_object = t.group('read_view.object')

g_object.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.create_space('test')
        box.space.test:create_index('pk')
        box.space.test:insert({1})
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        fiber.create(box.snapshot)
        local l = box.read_view.list()
        t.assert_equals(#l, 1)
        rawset(_G, 'test_rv', l[1])
    end)
end)

g_object.after_all(function(cg)
    cg.server:drop()
end)

-- Checks errors raised on invalid read view object usage.
g_object.test_usage = function(cg)
    cg.server:exec(function()
        local rv = rawget(_G, 'test_rv')
        t.assert_error_msg_equals(
            'Use read_view:info(...) instead of read_view.info(...)',
            rv.info)
        t.assert_error_msg_equals(
            'Use read_view:close(...) instead of read_view.close(...)',
            rv.close)
    end)
end

-- Checks read_view:info() method.
g_object.test_info = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local rv = rawget(_G, 'test_rv')
        local i = rv:info()
        t.assert_equals(i, {
            id = rv.id,
            name = rv.name,
            is_system = rv.is_system,
            timestamp = rv.timestamp,
            vclock = rv.vclock,
            signature = rv.signature,
            status = 'open',
        })
        t.assert_almost_equals(i.timestamp, fiber.clock(), 10)
        i.timestamp = nil
        t.assert_equals(i, {
            id = 2,
            name = 'checkpoint',
            is_system = true,
            vclock = box.info.vclock,
            signature = box.info.signature,
            status = 'open',
        })
    end)
end

-- Checks serialization of a read view object.
g_object.test_serialize = function(cg)
    cg.server:exec(function()
        local yaml = require('yaml')
        local rv = rawget(_G, 'test_rv')
        local actual = yaml.decode(yaml.encode(rv))
        local expected = rv:info()
        t.assert_almost_equals(actual.timestamp, expected.timestamp, 0.1)
        actual.timestamp = nil
        expected.timestamp = nil
        t.assert_equals(actual, expected)
    end)
end

-- Checks console auto-completion of a read view object.
g_object.test_autocomplete = function(cg)
    cg.server:exec(function()
        local function complete(s)
            return require('console').completion_handler(s, 0, #s)
        end
        t.assert_items_equals(complete('test_rv.'), {
            'test_rv.',
            'test_rv.id',
            'test_rv.name',
            'test_rv.is_system',
            'test_rv.timestamp',
            'test_rv.vclock',
            'test_rv.signature',
            'test_rv.status',
            'test_rv.info(',
            'test_rv.close(',
        })
        t.assert_items_equals(complete('test_rv:'), {
            'test_rv:',
            'test_rv:info(',
            'test_rv:close(',
        })
    end)
end

local g_status = t.group('read_view.status')

g_status.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('pk')
    end)
end)

g_status.after_all(function(cg)
    cg.server:drop()
end)

g_status.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_equals(#box.read_view.list(), 0)
        end)
    end)
end)

-- Checks read_view.status field.
g_status.test_status = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        box.space.test:replace({1, 'status'})
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.yield()
        local l = box.read_view.list()
        t.assert_equals(#l, 1)
        local rv = l[1]
        t.assert_equals(rv.status, 'open')
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f:join())
        t.assert_equals(rv.status, 'closed')
    end)
end

-- Checks read_view:close() method.
g_status.test_close = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        box.space.test:replace({1, 'close'})
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.yield()
        local l = box.read_view.list()
        t.assert_equals(#l, 1)
        local rv = l[1]
        t.assert_error_msg_equals('read view is busy', rv.close, rv)
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f:join())
        t.assert_error_msg_equals('read view is closed', rv.close, rv)
    end)
end

-- Checks that read view objects aren't pinned by the read view registry.
g_status.test_gc = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        box.space.test:replace({1, 'close'})
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.yield()
        local l = box.read_view.list()
        t.assert_equals(#l, 1)
        local gc = setmetatable({}, {__mode = 'v'})
        gc.rv = l[1]
        l = nil -- luacheck: ignore
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f:join())
        collectgarbage('collect')
        t.assert_equals(gc.rv, nil)
    end)
end

local g_list = t.group('read_view.list')

g_list.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('pk')
    end)
    cg.replica1 = server:new({
        alias = 'replica1',
        box_cfg = {replication = server.build_listen_uri('master')},
    })
    cg.replica2 = server:new({
        alias = 'replica2',
        box_cfg = {replication = server.build_listen_uri('master')},
    })
end)

g_list.after_all(function(cg)
    cg.server:drop()
    cg.replica1:drop()
    cg.replica2:drop()
end)

-- Checks box.read_view.list() output.
g_list.test_list = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.read_view.list(), {})
        box.error.injection.set('ERRINJ_ENGINE_JOIN_DELAY', true)
    end)
    cg.replica1:start({wait_until_ready = false})
    cg.server:exec(function()
        local fiber = require('fiber')
        t.helpers.retrying({}, function()
            t.assert_equals(#box.read_view.list(), 1)
        end)
        box.space.test:replace({1, 'list', 1})
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.yield()
        rawset(_G, 'snapshot_fiber', f)
        box.space.test:replace({1, 'list', 2})
    end)
    cg.replica2:start({wait_until_ready = false})
    cg.server:exec(function()
        local fiber = require('fiber')
        -- Check the list.
        t.helpers.retrying({}, function()
            t.assert_equals(#box.read_view.list(), 3)
        end)
        local l = box.read_view.list()
        local join1_rv = l[1]
        local snap1_rv = l[2]
        local join2_rv = l[3]
        t.assert_equals(join1_rv.name, 'join')
        t.assert_equals(join2_rv.name, 'join')
        t.assert_equals(snap1_rv.name, 'checkpoint')
        t.assert_equals(join1_rv.status, 'open')
        t.assert_equals(join2_rv.status, 'open')
        t.assert_equals(snap1_rv.status, 'open')

        -- Check that box.read_view.list() caches objects.
        l = box.read_view.list()
        t.assert_is(l[1], join1_rv)
        t.assert_is(l[2], snap1_rv)
        t.assert_is(l[3], join2_rv)

        -- Complete snapshot and check the list.
        local f = rawget(_G, 'snapshot_fiber')
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f:join())
        l = box.read_view.list()
        t.assert_equals(#l, 2)
        t.assert_is(l[1], join1_rv)
        t.assert_is(l[2], join2_rv)
        t.assert_equals(join1_rv.status, 'open')
        t.assert_equals(join2_rv.status, 'open')
        t.assert_equals(snap1_rv.status, 'closed')

        -- Start another snapshot and check the list.
        box.space.test:replace({1, 'list', 3})
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.yield()
        l = box.read_view.list()
        t.assert_equals(#l, 3)
        t.assert_is(l[1], join1_rv)
        t.assert_is(l[2], join2_rv)
        local snap2_rv = l[3]
        t.assert_equals(snap2_rv.name, 'checkpoint')
        t.assert_equals(join1_rv.status, 'open')
        t.assert_equals(join2_rv.status, 'open')
        t.assert_equals(snap1_rv.status, 'closed')
        t.assert_equals(snap2_rv.status, 'open')

        -- Complete replica join and check the list.
        box.error.injection.set('ERRINJ_ENGINE_JOIN_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[2])
            t.assert(box.info.replication[2].downstream)
            t.assert_equals(box.info.replication[2].downstream.status, 'follow')
            t.assert(box.info.replication[3])
            t.assert(box.info.replication[3].downstream)
            t.assert_equals(box.info.replication[3].downstream.status, 'follow')
        end)
        l = box.read_view.list()
        t.assert_equals(#l, 1)
        t.assert_is(l[1], snap2_rv)
        t.assert_equals(join1_rv.status, 'closed')
        t.assert_equals(join2_rv.status, 'closed')
        t.assert_equals(snap1_rv.status, 'closed')
        t.assert_equals(snap2_rv.status, 'open')

        -- Complete snapshot and check the list.
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f:join())
        t.assert_equals(box.read_view.list(), {})
        t.assert_equals(join1_rv.status, 'closed')
        t.assert_equals(join2_rv.status, 'closed')
        t.assert_equals(snap1_rv.status, 'closed')
        t.assert_equals(snap2_rv.status, 'closed')

        -- Check the read view id and signature order.
        t.assert_lt(join1_rv.id, snap1_rv.id)
        t.assert_lt(snap1_rv.id, join2_rv.id)
        t.assert_lt(join2_rv.id, snap2_rv.id)
        t.assert_lt(join1_rv.signature, snap1_rv.signature)
        t.assert_lt(snap1_rv.signature, join2_rv.signature)
        t.assert_lt(join2_rv.signature, snap2_rv.signature)
    end)
end
