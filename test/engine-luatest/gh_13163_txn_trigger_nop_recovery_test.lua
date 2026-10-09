local server = require('luatest.server')
local t = require('luatest')

local function setup_helpers()
    rawset(_G, 'capture_rows', function(result)
        return function(iterator)
            local rows = {}
            for num, old, new, space_id in iterator() do
                table.insert(rows, {num, old, new, space_id})
            end
            table.insert(result, rows)
        end
    end)

    rawset(_G, 'write_transactions', function(begin, finish)
        begin = begin or box.begin
        finish = finish or box.commit
        local s1, s2 = box.space.test1, box.space.test2
        local svp = begin()
        s1:replace{0, 'skip'}
        s1:replace{1, 'first'}
        s1:replace{1, 'skip'}
        s2:replace{2, 'second'}
        s1:replace{3, 'third'}
        s2:replace{2, 'skip'}
        finish(svp)

        -- All callbacks still run for a transaction containing only NOPs.
        svp = begin()
        s1:replace{1, 'skip'}
        s2:replace{2, 'skip'}
        finish(svp)
    end)
end

local function create_spaces(engine, space_type)
    for i = 1, 2 do
        local s = box.schema.space.create('test' .. i, {
            engine = engine, type = space_type, id = 511 + i,
        })
        s:create_index('pk')
        s:before_replace(function(old, new)
            if new ~= nil and new[2] == 'skip' then
                return old
            end
            return new
        end)
    end
end

--
-- Check that transaction triggers skip NOPs during WAL recovery, where they
-- have no associated space. The group can be dropped in 5.0, where
-- compat.box_recovery_triggers_deprecation will become obsolete.
--
local g = t.group('gh-13163', t.helpers.matrix({
    engine = {'memtx', 'vinyl'},
    event = {'before_commit', 'on_commit', 'on_rollback'},
    scope = {'global', 'name', 'multi', 'legacy'},
}))

g.before_all(function(cg)
    t.skip_if(cg.params.scope == 'legacy' and
              cg.params.event == 'before_commit',
              'Legacy API has no before_commit trigger')
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(setup_helpers)
    cg.server:exec(create_spaces, {cg.params.engine})
    cg.server:exec(function() box.snapshot() end)
    cg.server:exec(function() _G.write_transactions() end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

local function setup_triggers(event, scope)
    local trigger = require('trigger')
    require('compat').box_recovery_triggers_deprecation = 'old'
    rawset(_G, 'txn_rows', {})

    -- Exclude snapshot recovery and the test server's post-recovery grants.
    local collecting = false
    box.ctl.on_recovery_state(function(state)
        if state == 'snapshot_recovered' then
            collecting = true
        elseif state == 'wal_recovered' then
            collecting = false
        end
    end)

    local capture = _G.capture_rows(_G.txn_rows)
    local function on_txn(iterator)
        if collecting then
            capture(iterator)
        end
    end
    if scope == 'global' then
        trigger.set('box.' .. event, 'capture', on_txn)
    elseif scope ~= 'legacy' then
        trigger.set('box.' .. event .. '.space.test1', 'capture', on_txn)
        if scope == 'multi' then
            trigger.set('box.' .. event .. '.space[513]', 'capture', on_txn)
        end
    end

    if event == 'on_rollback' or scope == 'legacy' then
        trigger.set('box.before_commit', 'prepare', function()
            if not collecting then
                return
            end
            -- Legacy triggers need an active transaction for registration.
            if scope == 'legacy' then
                box[event](on_txn)
            end
            if event == 'on_rollback' then
                error('rollback NOP transaction')
            end
        end)
    end
end

g.test_recovery = function(cg)
    local event, scope = cg.params.event, cg.params.scope
    local before_box_cfg = ('loadstring(%q)(); loadstring(%q)(%q, %q)'):format(
        string.dump(setup_helpers), string.dump(setup_triggers), event, scope)
    cg.server:restart({
        box_cfg = {force_recovery = event == 'on_rollback'},
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg},
    })
    cg.server:exec(function(event, scope)
        local first, last = {1, 'first'}, {3, 'third'}
        if event == 'on_rollback' then
            first, last = last, first
        end
        local expected
        if scope == 'global' or scope == 'legacy' then
            expected = {{
                {1, nil, first, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, last, 512},
            }, {}}
        else
            expected = {{{1, nil, first, 512}, {2, nil, last, 512}}}
            if scope == 'multi' then
                table.insert(expected, {{1, nil, {2, 'second'}, 513}})
            end
        end
        t.assert_items_equals(_G.txn_rows, expected)
        if event == 'on_rollback' then
            t.assert_equals(box.space.test1:select(), {})
            t.assert_equals(box.space.test2:select(), {})
        else
            t.assert_equals(box.space.test1:select(), {
                {1, 'first'}, {3, 'third'},
            })
            t.assert_equals(box.space.test2:select(), {{2, 'second'}})
        end
    end, {event, scope})
end

--
-- Writes canceled by before_replace keep their space but must be skipped by
-- transaction iterators, even for temporary spaces without WAL rows. Check
-- commit, rollback, and rollback to a savepoint, callbacks still run with
-- empty iterators.
--
local g_runtime = t.group('gh-13163-runtime', {
    {engine = 'memtx', type = 'normal'},
    {engine = 'vinyl', type = 'normal'},
    {engine = 'memtx', type = 'data-temporary'},
    {engine = 'memtx', type = 'temporary'},
})

g_runtime.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(setup_helpers)
    cg.server:exec(create_spaces, {cg.params.engine, cg.params.type})
end)

g_runtime.after_all(function(cg)
    cg.server:drop()
end)

g_runtime.test_runtime = function(cg)
    cg.server:exec(function(space_type)
        local trigger = require('trigger')
        local capture = _G.capture_rows
        local s1, s2 = box.space.test1, box.space.test2
        local cases = {
            {'before_commit', 'commit'},
            {'on_commit', 'commit'},
            {'on_rollback', 'rollback'},
            {'on_rollback', 'rollback_to_savepoint'},
        }
        for _, case in ipairs(cases) do
            s1:truncate()
            s2:truncate()
            local event, action = unpack(case)
            local result = {global = {}, test1 = {}, test2 = {}, legacy = {}}
            local events = {
                global = 'box.' .. event,
                test1 = 'box.' .. event .. '.space.test1',
                test2 = 'box.' .. event .. '.space[513]',
            }
            for name, event_name in pairs(events) do
                trigger.set(event_name, 'capture', capture(result[name]))
            end
            local legacy_capture = capture(result.legacy)
            local function begin()
                box.begin()
                if event ~= 'before_commit' then box[event](legacy_capture) end
                return box.savepoint()
            end
            local function finish(svp)
                if action == 'rollback_to_savepoint' then
                    box.rollback_to_savepoint(svp)
                    box.commit()
                else
                    box[action]()
                end
            end

            _G.write_transactions(begin, finish)

            -- The first space's iterator is empty; the second has a real write.
            local svp = begin()
            s1:replace{4, 'skip'}
            s2:replace{5, 'fifth'}
            finish(svp)

            svp = begin()
            finish(svp)

            -- A missing-key DELETE is not an IPROTO_NOP.
            svp = begin()
            s1:delete{1000}
            finish(svp)

            local first, last = {1, 'first'}, {3, 'third'}
            if event == 'on_rollback' then first, last = last, first end
            local rows = {
                {1, nil, first, 512},
                {2, nil, {2, 'second'}, 513},
                {3, nil, last, 512},
            }
            local fifth = {{1, nil, {5, 'fifth'}, 513}}
            local missing = {{1, nil, nil, 512}}
            local expected = {
                global = {rows, {}, fifth, {}, missing},
                test1 = {{
                    {1, nil, first, 512}, {2, nil, last, 512},
                }, {}, {}, missing},
                test2 = {{{1, nil, {2, 'second'}, 513}}, {}, fifth},
                legacy = {},
            }
            if event ~= 'before_commit' then
                expected.legacy = space_type == 'normal' and expected.global or
                                  {{}, {}, {}, {}, {}}
            end
            t.assert_equals(result, expected)
            for _, event_name in pairs(events) do
                trigger.del(event_name, 'capture')
            end
        end
    end, {cg.params.type})
end

--
-- Replicated NOPs have no associated space even after recovery completes.
-- Transaction triggers must skip them.
--
local g_replication = t.group('gh-13163-replication', {
    {engine = 'memtx'}, {engine = 'vinyl'},
})

g_replication.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.master:exec(setup_helpers)
    cg.master:exec(create_spaces, {cg.params.engine})
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {replication = cg.master.net_box_uri},
    })
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(setup_helpers)
    cg.replica:exec(function()
        require('compat').box_recovery_triggers_deprecation = 'new'
        local result = {global = {}, test1 = {}, test2 = {}, legacy = {}}
        rawset(_G, 'txn_rows', result)
        local capture = _G.capture_rows
        local trigger = require('trigger')
        trigger.set('box.on_commit', 'capture', capture(result.global))
        trigger.set('box.on_commit.space.test1', 'capture',
                    capture(result.test1))
        trigger.set('box.on_commit.space[513]', 'capture',
                    capture(result.test2))
        local legacy_capture = capture(result.legacy)
        trigger.set('box.before_commit', 'prepare', function()
            box.on_commit(legacy_capture)
        end)
    end)
end)

g_replication.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

g_replication.test_replication = function(cg)
    cg.master:exec(function() _G.write_transactions() end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local rows = {
            {1, nil, {1, 'first'}, 512},
            {2, nil, {2, 'second'}, 513},
            {3, nil, {3, 'third'}, 512},
        }
        t.assert_equals(_G.txn_rows, {
            global = {rows, {}},
            legacy = {rows, {}},
            test1 = {{
                {1, nil, {1, 'first'}, 512},
                {2, nil, {3, 'third'}, 512},
            }},
            test2 = {{{1, nil, {2, 'second'}, 513}}},
        })
        t.assert_equals(box.space.test1:select(), {
            {1, 'first'}, {3, 'third'},
        })
        t.assert_equals(box.space.test2:select(), {{2, 'second'}})
    end)
end
