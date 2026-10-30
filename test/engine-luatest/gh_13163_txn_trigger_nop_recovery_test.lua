local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local function capture_rows(result)
    return function(iterator)
        local rows = {}
        for num, old, new, space_id in iterator() do
            table.insert(rows, {num, old, new, space_id})
        end
        table.insert(result, rows)
    end
end

local g = t.group('gh-13163')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {checkpoint_interval = 0}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_runtime = function(cg)
    cg.server:exec(function(capture_source)
        local capture_rows = loadstring(capture_source)
        local trigger = require('trigger')
        local s1 = box.schema.space.create('test1', {id = 512})
        local s2 = box.schema.space.create('test2', {
            id = 513, type = 'temporary',
        })
        for _, s in ipairs({s1, s2}) do
            s:create_index('pk')
            s:before_replace(function(old, new)
                if new ~= nil and new[2] == 'skip' then
                    return old
                end
                return new
            end)
        end

        local result = {global = {}, test1 = {}, test2 = {}, legacy = {}}
        trigger.set('box.on_commit', 'capture', capture_rows(result.global))
        trigger.set('box.on_commit.space.test1', 'capture',
                    capture_rows(result.test1))
        trigger.set('box.on_commit.space[513]', 'capture',
                    capture_rows(result.test2))

        box.begin()
        box.on_commit(capture_rows(result.legacy))
        s1:replace{0, 'skip'}
        s1:replace{1, 'first'}
        s1:replace{1, 'skip'}
        s2:replace{2, 'second'}
        s1:replace{3, 'third'}
        s2:replace{2, 'skip'}
        -- A missing-key DELETE is not an IPROTO_NOP.
        s1:delete{1000}
        box.commit()

        box.begin()
        box.on_commit(capture_rows(result.legacy))
        s1:replace{1, 'skip'}
        s2:replace{2, 'skip'}
        box.commit()

        t.assert_equals(result, {
            global = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, {3, 'third'}, 512},
                {4, nil, nil, 512},
            }, {}},
            test1 = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {3, 'third'}, 512},
                {3, nil, nil, 512},
            }, {}},
            test2 = {{{1, nil, {2, 'second'}, 513}}, {}},
            -- Legacy iterators skip all statements without a WAL row.
            legacy = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {3, 'third'}, 512},
                {3, nil, nil, 512},
            }, {}},
        })
        t.assert_equals(s1:select(), {{1, 'first'}, {3, 'third'}})
        t.assert_equals(s2:select(), {{2, 'second'}})

        trigger.del('box.on_commit', 'capture')
        trigger.del('box.on_commit.space.test1', 'capture')
        trigger.del('box.on_commit.space[513]', 'capture')
        s1:drop()
        s2:drop()
    end, {string.dump(capture_rows)})
end

-- This test can be dropped when recovery triggers become obsolete in 5.0.
g.test_recovery = function(cg)
    cg.server:exec(function()
        local s1 = box.schema.space.create('test1', {id = 512})
        local s2 = box.schema.space.create('test2', {id = 513})
        for _, s in ipairs({s1, s2}) do
            s:create_index('pk')
            s:before_replace(function(old, new)
                if new[2] == 'skip' then
                    return old
                end
                return new
            end)
        end
        box.snapshot()

        box.begin()
        s1:replace{0, 'skip'}
        s1:replace{1, 'first'}
        s1:replace{1, 'skip'}
        s2:replace{2, 'second'}
        s1:replace{3, 'third'}
        s2:replace{2, 'skip'}
        box.commit()

        box.begin()
        s1:replace{1, 'skip'}
        s2:replace{2, 'skip'}
        box.commit()
    end)

    local function setup_triggers(capture_source)
        local capture_rows = loadstring(capture_source)
        local trigger = require('trigger')
        require('compat').box_recovery_triggers_deprecation = 'old'
        local result = {global = {}, test1 = {}, test2 = {}, legacy = {}}
        rawset(_G, 'txn_rows', result)

        -- Exclude the snapshot and the test server's post-recovery grants.
        box.ctl.on_recovery_state(function(state)
            if state == 'snapshot_recovered' then
                trigger.set('box.on_commit', 'capture',
                            capture_rows(result.global))
                trigger.set('box.on_commit.space.test1', 'capture',
                            capture_rows(result.test1))
                trigger.set('box.on_commit.space[513]', 'capture',
                            capture_rows(result.test2))
                trigger.set('box.before_commit', 'prepare', function()
                    box.on_commit(capture_rows(result.legacy))
                end)
            elseif state == 'wal_recovered' then
                trigger.del('box.on_commit', 'capture')
                trigger.del('box.on_commit.space.test1', 'capture')
                trigger.del('box.on_commit.space[513]', 'capture')
                trigger.del('box.before_commit', 'prepare')
            end
        end)
    end
    local before_box_cfg = ('loadstring(%q)(%q)'):format(
        string.dump(setup_triggers), string.dump(capture_rows))
    cg.server:restart({
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg},
    })
    cg.server:exec(function()
        t.assert_equals(_G.txn_rows, {
            global = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, {3, 'third'}, 512},
            }, {}},
            test1 = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {3, 'third'}, 512},
            }},
            test2 = {{{1, nil, {2, 'second'}, 513}}},
            legacy = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, {3, 'third'}, 512},
            }, {}},
        })
        t.assert_equals(box.space.test1:select(), {{1, 'first'}, {3, 'third'}})
        t.assert_equals(box.space.test2:select(), {{2, 'second'}})
        box.space.test1:drop()
        box.space.test2:drop()
    end)
end

local g_replication = t.group('gh-13163-replication')

g_replication.before_all(function(cg)
    cg.replica_set = replica_set:new({})
    cg.server = cg.replica_set:build_and_add_server({
        alias = 'server',
        box_cfg = {replication_timeout = 0.1},
    })
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = cg.server.net_box_uri,
            replication_timeout = 0.1,
        },
    })
    cg.replica_set:start()
    cg.server:exec(function()
        local s1 = box.schema.space.create('test1', {id = 512})
        s1:create_index('pk')
        local s2 = box.schema.space.create('test2', {id = 513})
        s2:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.server)
end)

g_replication.after_all(function(cg)
    cg.replica_set:drop()
end)

g_replication.test_replication = function(cg)
    cg.replica:exec(function(capture_source)
        local capture_rows = loadstring(capture_source)
        local trigger = require('trigger')
        require('compat').box_recovery_triggers_deprecation = 'new'
        local result = {global = {}, test1 = {}, test2 = {}, legacy = {}}
        rawset(_G, 'txn_rows', result)
        trigger.set('box.on_commit', 'capture', capture_rows(result.global))
        trigger.set('box.on_commit.space.test1', 'capture',
                    capture_rows(result.test1))
        trigger.set('box.on_commit.space[513]', 'capture',
                    capture_rows(result.test2))
        trigger.set('box.before_commit', 'prepare', function()
            box.on_commit(capture_rows(result.legacy))
        end)
    end, {string.dump(capture_rows)})

    cg.server:exec(function()
        local s1, s2 = box.space.test1, box.space.test2
        for _, s in ipairs({s1, s2}) do
            s:before_replace(function(old, new)
                if new[2] == 'skip' then
                    return old
                end
                return new
            end)
        end

        box.begin()
        s1:replace{0, 'skip'}
        s1:replace{1, 'first'}
        s1:replace{1, 'skip'}
        s2:replace{2, 'second'}
        s1:replace{3, 'third'}
        s2:replace{2, 'skip'}
        box.commit()

        box.begin()
        s1:replace{1, 'skip'}
        s2:replace{2, 'skip'}
        box.commit()
    end)
    cg.replica:wait_for_vclock_of(cg.server)
    cg.replica:exec(function()
        t.assert_equals(_G.txn_rows, {
            global = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, {3, 'third'}, 512},
            }, {}},
            test1 = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {3, 'third'}, 512},
            }},
            test2 = {{{1, nil, {2, 'second'}, 513}}},
            legacy = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, {3, 'third'}, 512},
            }, {}},
        })
        t.assert_equals(box.space.test1:select(), {{1, 'first'}, {3, 'third'}})
        t.assert_equals(box.space.test2:select(), {{2, 'second'}})
    end)
end
