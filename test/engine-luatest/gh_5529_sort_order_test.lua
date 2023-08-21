local server = require('luatest.server')
local t = require('luatest')

local g_local = t.group('sort_order_test:local')

local g_each_engine = t.group('sort_order_test:each_engine',
    t.helpers.matrix({engine = {'memtx', 'vinyl'}}))

local g_sequential = t.group('sort_order_test:sequential', t.helpers.matrix({
    engine = {'memtx', 'vinyl'},
    is_unique = {true, false}
}))

local g_func_index = t.group('sort_order_test:func_index', t.helpers.matrix({
    engine = {'memtx'},
    is_nullable = {true, false},
    is_unique = {true, false}
}))

for _, g in pairs({g_local, g_sequential, g_func_index, g_each_engine}) do
    g.before_all(function(cg)
        cg.server = server:new({alias = 'default'})
        cg.server:start()
    end)

    g.after_all(function(cg)
        cg.server:drop()
    end)
end

g_func_index.before_test('test_regular', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('s', {engine = engine})
        s:create_index('pk', {parts = {
            {1, 'unsigned'},
            {2, 'unsigned'},
            {3, 'unsigned'},
            {4, 'unsigned'}
        }})
        for i = 1, 2 do
            for j = 1, 2 do
                for k = 1, 2 do
                    for l = 1, 2 do
                        s:insert({i, j, k, l})
                    end
                end
            end
        end
    end, {cg.params.engine})
end)

g_func_index.after_test('test_regular', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
        if box.func.f ~= nil then
            box.func.f:drop()
        end
    end)
end)

g_func_index.test_regular = function(cg)
    local expect = {
        {2, 1, 2, 1},
        {2, 1, 2, 2},
        {2, 1, 1, 1},
        {2, 1, 1, 2},
        {2, 2, 2, 1},
        {2, 2, 2, 2},
        {2, 2, 1, 1},
        {2, 2, 1, 2},
        {1, 1, 2, 1},
        {1, 1, 2, 2},
        {1, 1, 1, 1},
        {1, 1, 1, 2},
        {1, 2, 2, 1},
        {1, 2, 2, 2},
        {1, 2, 1, 1},
        {1, 2, 1, 2}
    }

    cg.server:exec(function(engine, is_nullable, is_unique, expect)
        t.assert(engine ~= nil)
        t.assert(is_nullable ~= nil)
        t.assert(is_unique ~= nil)
        local s = box.space.s
        local lua_code
        if is_nullable then
            lua_code = 'function(t) return {t[1], t[2],'
                       .. ' t[3] == 2 and t[3] or nil, t[4]} end'
        else
            lua_code = 'function(t) return {t[1], t[2], t[3], t[4]} end'
        end
        box.schema.func.create('f', {
            body = lua_code,
            is_deterministic = true,
            is_sandboxed = true
        })
        local idx = s:create_index('f', {parts = {
            {1, 'unsigned', sort_order = 'desc'},
            {2, 'unsigned', sort_order = 'asc'},
            {3, 'unsigned', sort_order = 'desc', is_nullable = is_nullable},
            {4, 'unsigned', sort_order = 'asc'}
        }, func = 'f', unique = is_unique})
        t.assert_equals(idx:select(), expect)
    end, {cg.params.engine, cg.params.is_nullable, cg.params.is_unique, expect})

    -- Assure the index is built the right way on the WAL recovery.
    cg.server:restart()
    cg.server:exec(function(expect)
        t.assert_equals(box.space.s.index.f:select(), expect)
    end, {expect})

    -- Assure the index is built the right way on the snapshot recovery.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    cg.server:exec(function(expect)
        t.assert_equals(box.space.s.index.f:select(), expect)
    end, {expect})
end

g_func_index.before_test('test_singlepart', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('s', {engine = engine})
        s:create_index('pk', {parts = {{1, 'unsigned'}}})
        for i = 1, 16 do
            s:insert({i})
        end
    end, {cg.params.engine})
end)

g_func_index.after_test('test_singlepart', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
        if box.func.f ~= nil then
            box.func.f:drop()
        end
    end)
end)

g_func_index.test_singlepart = function(cg)
    cg.server:exec(function(engine, is_nullable, is_unique)
        t.assert(engine ~= nil)
        t.assert(is_nullable ~= nil)
        t.assert(is_unique ~= nil)
        local s = box.space.s
        local lua_code = 'function(tuple) return {tuple[1]} end'
        box.schema.func.create('f', {
            body = lua_code,
            is_deterministic = true,
            is_sandboxed = true
        })
        local idx = s:create_index('f', {parts = {
            {1, 'unsigned', sort_order = 'desc', is_nullable = is_nullable},
        }, func = 'f', unique = is_unique})
        local expect = {{16}, {15}, {14}, {13}, {12}, {11}, {10}, {9},
                        {8},  {7},  {6},  {5},  {4},  {3},  {2},  {1}}
        t.assert_equals(idx:select(), expect)
        expect = {{8},  {7},  {6},  {5},  {4},  {3},  {2},  {1}}
        t.assert_equals(idx:select(9, {iterator = 'GT'}), expect)
        expect = {{9}, {10}, {11}, {12}, {13}, {14}, {15}, {16}}
        t.assert_equals(idx:select(8, {iterator = 'LT'}), expect)
        expect = {{9}, {8},  {7},  {6},  {5},  {4},  {3},  {2},  {1}}
        t.assert_equals(idx:select(9, {iterator = 'GE'}), expect)
        expect = {{8}, {9}, {10}, {11}, {12}, {13}, {14}, {15}, {16}}
        t.assert_equals(idx:select(8, {iterator = 'LE'}), expect)
    end, {cg.params.engine, cg.params.is_nullable, cg.params.is_unique})

    -- Assure the index is built the right way on the WAL recovery.
    cg.server:restart()
    cg.server:exec(function()
        local expect = {{16}, {15}, {14}, {13}, {12}, {11}, {10}, {9},
                        {8},  {7},  {6},  {5},  {4},  {3},  {2},  {1}}
        t.assert_equals(box.space.s.index.f:select(), expect)
        -- Add some data for the next test.
        box.space.s:insert({17})
        box.space.s:insert({18})
    end)

    -- Assure the index is built the right way on the snapshot recovery.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    cg.server:exec(function()
        local expect = {{18}, {17}, {16}, {15}, {14}, {13}, {12}, {11}, {10},
                        {9},  {8},  {7},  {6},  {5},  {4},  {3},  {2},  {1}}
        t.assert_equals(box.space.s.index.f:select(), expect)
    end)
end

g_sequential.before_test('test_sequential', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('s', {engine = engine})
        s:create_index('pk', {parts = {{4, 'unsigned'}}})
        local id = 1
        for i = 1, 2 do
            for j = 1, 2 do
                for k = 1, 2 do
                    s:insert({i, j, k, id})
                    id = id + 1
                end
            end
        end
    end, {cg.params.engine})
end)

g_sequential.after_test('test_sequential', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

g_sequential.test_sequential = function(cg)
    cg.server:exec(function(engine, is_unique)
        t.assert(engine ~= nil)
        t.assert(is_unique ~= nil)
        local s = box.space.s
        local idx = s:create_index('sk', {parts = {
            {1, 'unsigned', sort_order = 'desc'},
            {2, 'unsigned', sort_order = 'asc'},
            {3, 'unsigned', sort_order = 'desc'},
        }, unique = is_unique})
        local expect = {
            {2, 1, 2, 6},
            {2, 1, 1, 5},
            {2, 2, 2, 8},
            {2, 2, 1, 7},
            {1, 1, 2, 2},
            {1, 1, 1, 1},
            {1, 2, 2, 4},
            {1, 2, 1, 3}
        }
        t.assert_equals(idx:select(), expect)
    end, {cg.params.engine, cg.params.is_unique})
end

g_each_engine.before_test('test_alter_sort_order', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('s', {engine = engine})
        s:create_index('pk', {parts = {{1, 'unsigned'}}})
        s:create_index('i', {parts = {{1, 'unsigned'}}})
        for i = 1, 10 do
            s:insert({i})
        end
    end, {cg.params.engine})
end)

g_each_engine.after_test('test_alter_sort_order', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

g_each_engine.test_alter_sort_order = function(cg)
    cg.server:exec(function(engine)
        local data_asc = {}
        local data_desc = {}
        for i = 1, 10 do
            data_asc[i] = {i}
            data_desc[11 - i] = {i}
        end
        local s = box.space.s

        local i = s.index.i
        t.assert_equals(i:select(), data_asc)
        i:alter({parts = {{1, 'unsigned', sort_order = 'desc'}}})
        t.assert_equals(i:select(), data_desc)
        i:alter({parts = {{1, 'unsigned', sort_order = 'asc'}}})
        t.assert_equals(i:select(), data_asc)

        -- Vynil does not support the primary index rebuild.
        if engine ~= 'vinyl' then
            i = s.index.pk
            t.assert_equals(i:select(), data_asc)
            i:alter({parts = {{1, 'unsigned', sort_order = 'desc'}}})
            t.assert_equals(i:select(), data_desc)
            i:alter({parts = {{1, 'unsigned', sort_order = 'asc'}}})
            t.assert_equals(i:select(), data_asc)
        end
    end, {cg.params.engine})
end

g_local.after_test('test_box_api', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

g_local.test_box_api = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s', {engine = 'memtx'})
        local msg = 'Wrong index part 1: unknown sort order'
        t.assert_error_msg_contains(msg, s.create_index, s, 'i', {parts = {
            {1, 'unsigned', sort_order = 'invalid'},
        }})
        msg = 'Wrong index part 1: unordered indexes do not support sort_order'
        t.assert_error_msg_contains(msg, s.create_index, s, 'i', {
            type = 'HASH',
            parts = {{1, 'unsigned', sort_order = 'asc'}},
        })
        t.assert_error_msg_contains(msg, s.create_index, s, 'i', {
            type = 'RTREE',
            parts = {{1, 'unsigned', sort_order = 'asc'}},
        })
        t.assert_error_msg_contains(msg, s.create_index, s, 'i', {
            type = 'BITSET',
            parts = {{1, 'unsigned', sort_order = 'asc'}},
        })
    end)
end

g_local.after_test('test_infinities_sort_order', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

g_local.test_infinities_sort_order = function(cg)
    cg.server:exec(function()
        -- Test that hints don't violate the correct order of
        -- infinite values if the part is descending.
        local s = box.schema.space.create('s', {engine = 'memtx'})
        local desc = s:create_index('desc', {parts = {
            {1, 'number', sort_order = 'desc'}
        }})
        s:insert{math.huge}
        s:insert{-math.huge}
        s:insert{1e100}
        local expect = {{math.huge}, {1e100}, {-math.huge}}
        t.assert_equals(desc:select(), expect)
    end)
end
