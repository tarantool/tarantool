local t = require('luatest')
local server = require('luatest.server')

local function start_server(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert{1, 0}
    end)
end

local function drop_server(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.server:drop()
end

-- With WAL delayed, prepare an update in a user space, then DDL on another
-- space. A read-confirmed transaction reads the old user tuple and enters a
-- read view preceding both changes. The schema cache already reflects the DDL,
-- so _space and _vspace must match it despite the read view, while the user
-- tuple must remain unchanged for the reader.
--
-- operation selects the DDL: create, drop, or rename the target space.
-- committed selects the state of the writer and DDL transactions when checking
-- system spaces: false keeps both prepared with WAL delayed, true releases WAL
-- and waits for both to commit after establishing the reader's read view.
local g = t.group('system_space_read_view', t.helpers.matrix{
    operation = {'create', 'drop', 'rename'},
    committed = {false, true},
})

g.before_each(start_server)
g.after_each(drop_server)

g.test_cache_consistency = function(cg)
    cg.server:exec(function(operation, committed)
        local fiber = require('fiber')
        local s = box.space.test
        local id = 1000
        if operation ~= 'create' then
            box.schema.space.create('target', {id = id})
        end
        local count = box.space._space:len()
        local expected_name = 'target'
        if operation == 'create' then
            count = count + 1
        elseif operation == 'drop' then
            count = count - 1
            expected_name = nil
        else
            expected_name = 'renamed'
        end

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local writer = fiber.create(function()
            fiber.self():set_joinable(true)
            s:replace{1, 1}
        end)
        local ddl = fiber.create(function()
            fiber.self():set_joinable(true)
            if operation == 'create' then
                box.schema.space.create('target', {id = id})
            elseif operation == 'drop' then
                box.space.target:drop()
            else
                box.space.target:rename('renamed')
            end
        end)

        box.begin{txn_isolation = 'read-confirmed'}
        -- The read view precedes the DDL, but the cache already reflects it.
        t.assert_equals(s:get{1}, {1, 0})
        if committed then
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.assert(writer:join())
            t.assert(ddl:join())
        end
        local cached = box.space[id]
        t.assert_equals(cached and cached.name, expected_name)
        for _, name in ipairs({'_space', '_vspace'}) do
            local sys = box.space[name]
            local tuple = sys:get{id}
            t.assert_equals(tuple and tuple[3], expected_name)
            local expected = tuple and {tuple} or {}
            t.assert_equals(sys:select{id}, expected)
            t.assert_equals(sys:count{id}, #expected)
            t.assert_equals(sys:count(), count)
            local target = operation == 'create' and expected or {}
            t.assert_equals(sys.index.name:select{'target'}, target)
            if operation == 'rename' then
                t.assert_equals(sys.index.name:get{'renamed'}, tuple)
                t.assert_equals(sys.index.name:select{'renamed'}, expected)
            end
        end
        t.assert_equals(box.space._space:len(), count)
        t.assert_equals(s:get{1}, {1, 0})
        -- This transaction only read data, so its commit does not write to
        -- WAL and does not block even if committed is false and WAL is delayed.
        box.commit()

        if not committed then
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.assert(writer:join())
            t.assert(ddl:join())
        end
    end, {cg.params.operation, cg.params.committed})
end

local g_sql = t.group('sql_constraint_read_view')

g_sql.before_each(start_server)
g_sql.after_each(drop_server)

g_sql.test_constraint_on_prepared_space = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        t.assert(box.execute('CREATE TABLE parent(id INTEGER PRIMARY KEY)'))
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local writer = fiber.create(function()
            fiber.self():set_joinable(true)
            s:replace{1, 1}
        end)
        local ddl = fiber.create(function()
            fiber.self():set_joinable(true)
            t.assert(box.execute([[
                CREATE TABLE child(id INTEGER PRIMARY KEY, parent_id INTEGER)
            ]]))
        end)
        box.begin{txn_isolation = 'read-confirmed'}
        t.assert_not_equals(box.space.child, nil)
        t.assert_equals(s:get{1}, {1, 0})
        -- A read view cannot write, but SQL must find the cached space first.
        local result, err = box.execute([[
            ALTER TABLE child ADD CONSTRAINT fk
            FOREIGN KEY(parent_id) REFERENCES parent(id)
        ]])
        t.assert_equals(result, nil)
        -- Transactions in a read view are not allowed to write.
        t.assert_equals(err.code, box.error.TRANSACTION_CONFLICT)
        box.rollback()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(writer:join())
        t.assert(ddl:join())
    end)
end
