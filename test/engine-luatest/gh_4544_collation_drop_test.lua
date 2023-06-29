local t = require('luatest')

local function before_all(cg)
    local server = require('luatest.server')
    cg.server = server:new()
    cg.server:start()
end

local function after_all(cg)
    cg.server:drop()
end

local g1 = t.group('gh-4544-1')
g1.before_all(before_all)
g1.after_all(after_all)

-- Check that key_def compare doesn't crash after collation drop.
g1.test_keydef_crash = function(cg)
    cg.server:exec(function()
        local key_def = require('key_def')
        local coll_name = 'unicode_af_s1'
        local kd = key_def.new({{field = 1, type = 'string',
                                 collation = coll_name}})
        box.internal.collation.drop(coll_name)
        kd:compare({'a'}, {'b'})
        t.assert_equals(kd:totable()[1].collation, '<deleted>')
    end)
end

-- Replace old collation, which is used by key_def, by the new one with the same
-- fingerprint.
g1.test_keydef_replace_coll_same = function(cg)
    cg.server:exec(function()
        local key_def = require('key_def')
        local old_name = 'my old coll'
        local new_name = 'my new coll'
        box.internal.collation.create(old_name, 'ICU', '', {})
        local kd = key_def.new({{field = 1, type = 'string',
                                 collation = old_name}})
        box.internal.collation.drop(old_name)
        box.internal.collation.create(new_name, 'ICU', '', {})
        t.assert_equals(kd:totable()[1].collation, new_name)
        box.internal.collation.drop(new_name)
    end)
end

-- Replace old collation, which is used by key_def, by the new one with
-- different fingerprint.
g1.test_keydef_replace_coll_different = function(cg)
    cg.server:exec(function()
        local key_def = require('key_def')
        local old_name = 'my old coll'
        local new_name = 'my new coll'
        box.internal.collation.create(old_name, 'ICU', '', {})
        local kd = key_def.new({{field = 1, type = 'string',
                                 collation = old_name}})
        box.internal.collation.drop(old_name)
        box.internal.collation.create(new_name, 'ICU', 'ru-RU', {})
        t.assert_equals(kd:totable()[1].collation, '<deleted>')
        box.internal.collation.drop(new_name)
    end)
end

local g2 = t.group('gh-4544-2', {{engine = 'memtx'}, {engine = 'vinyl'}})
g2.before_all(before_all)
g2.after_all(after_all)

-- Pin/unpin collation by the space format, but not by the index.
g2.test_coll_pin_format = function(cg)
    local coll_name = 'my coll 1'

    local function init()
        cg.server:exec(function(engine, coll_name)
            box.internal.collation.create(coll_name, 'ICU', 'ru-RU', {})
            local s = box.schema.space.create('test', {engine = engine})
            s:format({{name = 'p'},
                      {name = 's', type = 'string', collation = coll_name}})
            s:create_index('pk')
        end, {cg.params.engine, coll_name})
    end

    local function check_references()
        cg.server:exec(function(coll_name)
            t.assert_error_msg_equals(
                "Can't drop collation '" .. coll_name .. "': collation is " ..
                "referenced by space format",
                box.internal.collation.drop, coll_name)
        end, {coll_name})
    end

    -- Check that collation can not be dropped.
    init()
    check_references()

    cg.server:restart()
    check_references()

    cg.server:eval('box.snapshot()')
    cg.server:restart()
    check_references()

    -- Check that collation is unpinned on space drop.
    cg.server:exec(function(coll_name)
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
    end, {coll_name})

    -- Check that collation can be dropped in one transaction with space drop.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
        box.commit()
    end, {coll_name})

    -- Check that collation is still pinned after rollback.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
        box.rollback()
    end, {coll_name})
    check_references()

    -- Check that collation is unpinned on space alter.
    cg.server:exec(function(coll_name)
        box.space.test:alter({format = {{name = 'p'}, {name = 's'}}})
        box.internal.collation.drop(coll_name)
        box.space.test:drop()
    end, {coll_name})

    -- Check that collation can be dropped in one transaction with space alter.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:alter({format = {{name = 'p'}, {name = 's'}}})
        box.internal.collation.drop(coll_name)
        box.commit()
        box.space.test:drop()
    end, {coll_name})

    -- Check that collation is still pinned after rollback.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:alter({format = {{name = 'p'}, {name = 's'}}})
        box.internal.collation.drop(coll_name)
        box.rollback()
    end, {coll_name})
    check_references()

    -- Cleanup.
    cg.server:exec(function(coll_name)
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
    end, {coll_name})
end

-- Pin/unpin collation by the index, but not by the space format.
g2.test_coll_pin_index = function(cg)
    local coll_name = 'my coll 2'

    local function init()
        cg.server:exec(function(engine, coll_name)
            box.internal.collation.create(coll_name, 'ICU', 'ru-RU', {})
            local s = box.schema.space.create('test', {engine = engine})
            s:create_index('pk')
            s:create_index('sk', {parts = {2, 'string',
                                           collation = coll_name}})
        end, {cg.params.engine, coll_name})
    end

    local function check_references()
        cg.server:exec(function(coll_name)
            t.assert_error_msg_equals(
                "Can't drop collation '" .. coll_name .. "': collation is " ..
                "referenced by index",
                box.internal.collation.drop, coll_name)
        end, {coll_name})
    end

    -- Check that collation can not be dropped.
    init()
    check_references()

    cg.server:restart()
    check_references()

    cg.server:eval('box.snapshot()')
    cg.server:restart()
    check_references()

    -- Check that collation is unpinned on space drop.
    cg.server:exec(function(coll_name)
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
    end, {coll_name})

    -- Check that collation can be dropped in one transaction with space drop.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
        box.commit()
    end, {coll_name})

    -- Check that collation is still pinned after rollback.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
        box.rollback()
    end, {coll_name})
    check_references()

    -- Check that collation is unpinned on index drop.
    cg.server:exec(function(coll_name)
        box.space.test.index.sk:drop()
        box.internal.collation.drop(coll_name)
        box.space.test:drop()
    end, {coll_name})

    -- Check that collation can be dropped in one transaction with index drop.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test.index.sk:drop()
        box.internal.collation.drop(coll_name)
        box.commit()
        box.space.test:drop()
    end, {coll_name})

    -- Check that collation is still pinned after rollback.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test.index.sk:drop()
        box.internal.collation.drop(coll_name)
        box.rollback()
    end, {coll_name})
    check_references()

    -- Cleanup.
    cg.server:exec(function(coll_name)
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
    end, {coll_name})
end

-- Pin/unpin collation by both: space format and the index.
g2.test_coll_pin_format_index = function(cg)
    local coll_name = 'my coll 3'

    local function init()
        cg.server:exec(function(engine, coll_name)
            box.internal.collation.create(coll_name, 'ICU', 'ru-RU', {})
            local s = box.schema.space.create('test', {engine = engine})
            s:format({{name = 'p'},
                      {name = 's', type = 'string', collation = coll_name}})
            s:create_index('pk')
            s:create_index('sk', {parts = {'s'}})
        end, {cg.params.engine, coll_name})
    end

    local function check_references()
        cg.server:exec(function(coll_name)
            t.assert_error_msg_equals(
                "Can't drop collation '" .. coll_name .. "': collation is " ..
                "referenced by space format",
                box.internal.collation.drop, coll_name)
        end, {coll_name})
    end

    -- Check that collation can not be dropped.
    init()
    check_references()

    cg.server:restart()
    check_references()

    cg.server:eval('box.snapshot()')
    cg.server:restart()
    check_references()

    -- Check that collation is unpinned on space drop.
    cg.server:exec(function(coll_name)
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
    end, {coll_name})

    -- Check that collation can be dropped in one transaction with space drop.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
        box.commit()
    end, {coll_name})

    -- Check that collation is still pinned after rollback.
    init()
    check_references()
    cg.server:exec(function(coll_name)
        box.begin()
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
        box.rollback()
    end, {coll_name})
    check_references()

    -- Check that collation is still pinned after index drop.
    cg.server:exec(function()
        box.space.test.index.sk:drop()
    end)
    check_references()

    -- Cleanup.
    cg.server:exec(function(coll_name)
        box.space.test:drop()
        box.internal.collation.drop(coll_name)
    end, {coll_name})
end

-- Check that collation is pinned from SQL.
g2.test_sql = function(cg)
    cg.server:exec(function(engine)
        local coll_name = 'unicode_af_s2'
        local sql = [[CREATE TABLE test (id STRING COLLATE "%s" PRIMARY KEY)
                      WITH ENGINE = '%s']]
        box.execute(string.format(sql, coll_name, engine))
        t.assert_error_msg_equals(
            "Can't drop collation 'unicode_af_s2': collation is referenced " ..
            "by space format",
            box.internal.collation.drop, coll_name)

        -- Check that collation is unpinned on table drop.
        box.execute("DROP TABLE test")
        box.internal.collation.drop(coll_name)
    end, {cg.params.engine})
end
