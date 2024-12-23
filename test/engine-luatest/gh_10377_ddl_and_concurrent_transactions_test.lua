local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix{
    engine = {'memtx', 'vinyl'},
    -- Whether DDL should abort the main operation.
    conflicting_ddl = {true, false},
    -- Whether DDL should do a DML statement as well.
    ddl_with_dml = {true, false},
    -- Whether we should prepare DML.
    prepare_dml = {true, false},
})

g.before_all({prepare_dml = false}, function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
    cg.server:exec(function(conflicting_ddl, ddl_with_dml)
        local fiber = require('fiber')
        -- A function that accepts an operation and checks if it is aborted
        -- by related DDL and not aborted by unrelated one. Note that the DDL
        -- can call the operation as well in order to check if it doesn't abort
        -- itself.
        rawset(_G, 'check_case', function(op)
            -- Open a transaction and do the operation on the first space.
            box.begin()
            op(box.space.test1)

            -- Process a concurrent transaction doing a DDL. Does the operation
            -- depending on test parameter to check if it doesn't abort itself.
            local f = fiber.new(function()
                local space = conflicting_ddl and box.space.test1
                                              or box.space.test2
                if ddl_with_dml then
                    op(space)
                end
                space:create_index('sk', {parts = {2, 'unsigned'}})
            end)
            f:set_joinable(true)
            t.assert_equals({f:join()}, {true})

            -- Check if commit behaves as expected.
            if conflicting_ddl then
                t.assert_error_covers({
                    type = 'ClientError',
                    code = box.error.TRANSACTION_CONFLICT,
                }, box.commit)
            else
                t.assert_equals({pcall(box.commit)}, {true})
            end
        end)
    end, {cg.params.conflicting_ddl, cg.params.ddl_with_dml})
end)

g.before_all({prepare_dml = true}, function(cg)
    t.tarantool.skip_if_not_debug()
    -- There is no point to test with non-conflicting DDL or with
    -- DDL-with-DML - those cases are already covered and this test
    -- is only about prepared DML.
    t.skip_if(not cg.params.conflicting_ddl or cg.params.ddl_with_dml,
              "excluded from matrix")

    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')
        -- A function that accepts an operation and checks if it is not aborted
        -- by a concurrent DDL if it's prepared.
        rawset(_G, 'check_case', function(op)
            local space = box.space.test1
            box.begin()
            op(space)

            local f = fiber.new(function()
                box.begin()
                -- Non-yielding alter (others can yield on vinyl spaces).
                space:alter{is_sync = true}
                box.error.injection.set('ERRINJ_WAL_DELAY', false)
                box.commit()
            end)
            f:set_joinable(true)

            box.error.injection.set('ERRINJ_WAL_DELAY', true)
            t.assert_equals({pcall(box.commit)}, {true})
            t.assert_equals({f:join()}, {true})
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(engine)
        box.schema.space.create('test1', {engine = engine})
        box.space.test1:create_index('pk')
        box.schema.space.create('test2', {engine = engine})
        box.space.test2:create_index('pk')
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test1 ~= nil then
            box.space.test1:drop()
        end
        if box.space.test2 ~= nil then
            box.space.test2:drop()
        end
    end)
end)

g.test_ddl_and_writers = function(cg)
    cg.server:exec(function()
        local i = 0
        local function insert_tuple(space)
            i = i + 1
            return space:replace({i, i})
        end
        _G.check_case(insert_tuple)
    end)
end

g.test_ddl_and_noop_writers = function(cg)
    cg.server:exec(function()
        local i = 0
        local function delete_nothing(space)
            i = i + 1
            return space:delete(i)
        end
        _G.check_case(delete_nothing)
    end)
end

g.test_ddl_and_tuple_readers = function(cg)
    t.skip_if(cg.params.engine == 'vinyl', 'gh-10786')
    cg.server:exec(function()
        box.space.test1:insert{1, 1}
        box.space.test2:insert{1, 1}
        local function get_existing_tuple(space)
            return space:get(1)
        end
        _G.check_case(get_existing_tuple)
    end)
end

g.test_ddl_aborts_related_point_readers = function(cg)
    t.skip_if(cg.params.engine == 'vinyl', 'gh-10786')
    cg.server:exec(function()
        local function get_non_existing_tuple(space)
            return space:get(1)
        end
        _G.check_case(get_non_existing_tuple)
    end)
end

g.test_ddl_aborts_related_fullscan_readers = function(cg)
    t.skip_if(cg.params.engine == 'vinyl', 'gh-10786')
    cg.server:exec(function()
        local function empty_fullscan(space)
            return space:select()
        end
        _G.check_case(empty_fullscan)
    end)
end

-- Gap with successor (a tuple greater than the searched range)
-- is a separate case since it is handled differently in memtx MVCC.
g.test_ddl_aborts_related_gap_with_successor_readers = function(cg)
    t.skip_if(cg.params.engine == 'vinyl', 'gh-10786')
    cg.server:exec(function()
        box.space.test1:insert{20, 20}
        box.space.test2:insert{20, 20}
        local function fullscan_with_successor(space)
            space:select(10, {iterator = 'LT'})
        end
        _G.check_case(fullscan_with_successor)
    end)
end
