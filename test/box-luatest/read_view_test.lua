local server = require('luatest.server')
local t = require('luatest')

local g_tweaks = t.group('read_view.tweaks')

g_tweaks.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g_tweaks.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that the box_read_view_ffi tweak does switch between Lua C and FFI
-- implementations of read view methods.
g_tweaks.test_ffi = function(cg)
    cg.server:exec(function()
        local tweaks = require('internal.tweaks')
        t.assert_equals(tweaks.box_read_view_ffi, true)
        local rv1 = box.read_view.open()
        local i1 = rv1.space._space.index.primary
        tweaks.box_read_view_ffi = false
        local rv2 = box.read_view.open()
        local i2 = rv2.space._space.index.primary
        t.assert(i1._ffi)
        t.assert_not(i2._ffi)
        for _, method in ipairs({'get', 'select', 'pairs'}) do
            t.assert_is_not(i1[method], i2[method], method)
        end
        rv1:close()
        rv2:close()
    end)
end

g_tweaks.after_test('test_ffi', function(cg)
    cg.server:exec(function()
        local tweaks = require('internal.tweaks')
        tweaks.box_read_view_ffi = true
    end)
end)

local g = t.group('read_view', t.helpers.matrix({ffi = {true, false}}))

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(ffi)
        local tweaks = require('internal.tweaks')
        tweaks.box_read_view_ffi = ffi
    end, {cg.params.ffi})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        -- If there's a stray read view left from a failing test,
        -- make sure it's gone.
        for _ = 1, 5 do collectgarbage('collect') end
        t.assert_equals(box.read_view.list(), {})
    end)
end)

-- Checks errors raised when an invalid option is passed to
-- box.read_view.open().
g.test_invalid_opts = function(cg)
    cg.server:exec(function()
        t.assert_error_msg_equals(
            "options should be a table", box.read_view.open, 'foo')
        t.assert_error_msg_equals(
            "unexpected option 'foo'", box.read_view.open, {foo = 'bar'})
        t.assert_error_msg_equals(
            "options parameter 'name' should be of type string",
            box.read_view.open, {name = 42})
    end)
end

-- Checks errors raised on invalid usage of object methods.
g.test_invalid_usage = function(cg)
    cg.server:exec(function()
        local rv = box.read_view.open()
        t.assert_error_msg_contains(
            "Use read_view:close(...) instead of read_view.close(...)",
            rv.close)
        t.assert_error_msg_contains(
            "Use read_view:info(...) instead of read_view.info(...)",
            rv.info)
        t.assert_error_msg_contains(
            "Use index:get(...) instead of index.get(...)",
            rv.space._space.index.primary.get)
        t.assert_error_msg_contains(
            "Use index:select(...) instead of index.select(...)",
            rv.space._space.index.primary.select)
        t.assert_error_msg_contains(
            "Use index:pairs(...) instead of index.pairs(...)",
            rv.space._space.index.primary.pairs)
        t.assert_error_msg_contains(
            "Use space:get(...) instead of space.get(...)",
            rv.space._space.get)
        t.assert_error_msg_contains(
            "Use space:select(...) instead of space.select(...)",
            rv.space._space.select)
        t.assert_error_msg_contains(
            "Use space:pairs(...) instead of space.pairs(...)",
            rv.space._space.pairs)
        rv:close()
    end)
end

-- Checks read view identifier.
g.test_id = function(cg)
    cg.server:exec(function()
        local rv1 = box.read_view.open()
        local rv2 = box.read_view.open()
        local rv3 = box.read_view.open()
        t.assert_type(rv1.id, 'number')
        t.assert_type(rv2.id, 'number')
        t.assert_type(rv3.id, 'number')
        t.assert_equals(rv2.id, rv1.id + 1)
        t.assert_equals(rv3.id, rv2.id + 1)
        rv1:close()
        rv2:close()
        rv3:close()
        local rv4 = box.read_view.open()
        t.assert_equals(rv4.id, rv3.id + 1)
        rv4:close()
    end)
end

-- Checks read view name.
g.test_name = function(cg)
    cg.server:exec(function()
        local rv = box.read_view.open()
        t.assert_equals(rv.name, 'unknown')
        rv:close()
        rv = box.read_view.open({name = 'test_name'})
        t.assert_equals(rv.name, 'test_name')
        rv:close()
    end)
end

-- Checks read view info.
g.test_info = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local rv = box.read_view.open()
        t.assert_equals(rv.status, 'open')
        t.assert_equals(rv.timestamp, fiber.clock())
        t.assert_equals(rv.vclock, box.info.vclock)
        t.assert_equals(rv.signature, box.info.signature)
        local info = rv:info()
        t.assert_equals(info, getmetatable(rv).__serialize(rv))
        t.assert_equals(info, {
            id = rv.id,
            name = rv.name,
            is_system = false,
            status = rv.status,
            timestamp = rv.timestamp,
            vclock = rv.vclock,
            signature = rv.signature,
        })
        rv:close()
        t.assert_equals(rv.status, 'closed')
    end)
end

-- Checks console auto-completion of a read view object.
g.test_autocomplete = function(cg)
    cg.server:exec(function()
        local function complete(s)
            return require('console').completion_handler(s, 0, #s)
        end
        local rv = box.read_view.open()
        rawset(_G, 'test_rv', rv)
        t.assert_items_equals(complete('test_rv.'), {
            'test_rv.',
            'test_rv.id',
            'test_rv.name',
            'test_rv.is_system',
            'test_rv.timestamp',
            'test_rv.vclock',
            'test_rv.signature',
            'test_rv.status',
            'test_rv.space',
            'test_rv.info(',
            'test_rv.close(',
        })
        t.assert_items_equals(complete('test_rv:'), {
            'test_rv:',
            'test_rv:info(',
            'test_rv:close(',
        })
        rv:close()
    end)
end

g.after_test('test_autocomplete', function(cg)
    cg.server:exec(function()
        rawset(_G, 'test_rv', nil)
    end)
end)

-- Checks read view list.
g.test_list = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.read_view.list(), {})
        local rv1 = box.read_view.open({name = 'foo'})
        local rv2 = box.read_view.open({name = 'bar'})
        local rv3 = box.read_view.open({name = 'baz'})
        t.assert_equals(box.read_view.list(), {rv1, rv2, rv3})
        rv2:close()
        t.assert_equals(box.read_view.list(), {rv1, rv3})
        rv1 = nil -- luacheck: ignore
        rv3 = nil -- luacheck: ignore
        for _ = 1, 5 do collectgarbage('collect') end
        t.assert_equals(box.read_view.list(), {})
    end)
end

-- Checks the list of spaces and indexes visible through a read view object.
g.test_space_list = function(cg)
    cg.server:exec(function()
        -- Create a few spaces.
        box.schema.space.create('test1')
        box.schema.space.create('test2')
        box.space.test2:create_index('primary')
        box.schema.space.create('test3')
        box.space.test3:create_index('primary')
        box.space.test3:create_index('secondary', {
            unique = false, parts = {2, 'unsigned'}
        })
        -- Make a copy of the space list before opening a read view.
        local spaces = table.deepcopy(box.space)
        local rv = box.read_view.open()
        -- These changes shouldn't be visible from the read view.
        box.schema.space.create('test4')
        box.space.test4:create_index('primary')
        box.space.test3.index.secondary:drop()
        box.space.test2:drop()
        box.space.test1:create_index('primary')
        -- Check the read view space list.
        t.assert_type(rv.space, 'table', 'space')
        for sid, space in pairs(spaces) do
            if space.engine ~= 'memtx' then
                goto continue
            end
            local msg = string.format('space[%s]', sid)
            t.assert_type(rv.space[sid], 'table', msg)
            t.assert_equals(rv.space[sid].id, space.id, msg .. '.id')
            t.assert_equals(rv.space[sid].name, space.name, msg .. '.name')
            t.assert_type(rv.space[sid].index, 'table', msg .. '.index')
            for iid, index in pairs(space.index) do
                msg = string.format('space[%s].index[%s]', sid, iid)
                t.assert_type(rv.space[sid].index[iid], 'table', msg)
                t.assert_equals(rv.space[sid].index[iid].id, index.id,
                                msg .. '.id')
                t.assert_equals(rv.space[sid].index[iid].name, index.name,
                                msg .. '.name')
            end
            ::continue::
        end
        rv:close()
    end)
end

g.after_test('test_space_list', function(cg)
    cg.server:exec(function()
        if box.space.test1 then
            box.space.test1:drop()
        end
        if box.space.test2 then
            box.space.test2:drop()
        end
        if box.space.test3 then
            box.space.test3:drop()
        end
        if box.space.test4 then
            box.space.test4:drop()
        end
    end)
end)

-- Checks that __serialize hides private fields and preserves aliases.
g.test_serialize = function(cg)
    cg.server:exec(function()
        local function serialize(x)
            local yaml = require('yaml')
            return yaml.decode(yaml.encode(x))
        end
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.space.test:create_index('sk', {parts = {2, 'unsigned'}})
        local rv = box.read_view.open()
        local actual_pk = serialize(rv.space.test.index.pk)
        local actual_sk = serialize(rv.space.test.index.sk)
        local actual_space = serialize(rv.space.test)
        local actual_space_list = serialize(rv.space)
        rv:close()
        local expected_pk = {
            id = 0,
            name = 'pk',
        }
        local expected_sk = {
            id = 1,
            name = 'sk',
        }
        local expected_space = {
            id = box.space.test.id,
            name = 'test',
            index = {
                [0] = expected_pk,
                [1] = expected_sk,
                pk = expected_pk,
                sk = expected_sk,
            },
        }
        local expected_space_list = {
            test = {
                id = box.space.test.id,
            },
        }
        -- Check values.
        t.assert_equals(actual_pk, expected_pk)
        t.assert_equals(actual_sk, expected_sk)
        t.assert_equals(actual_space, expected_space)
        t.assert_equals(actual_space_list, expected_space_list)
        -- Check aliases.
        t.assert_is(actual_space.index.pk, actual_space.index[0])
        t.assert_is(actual_space.index.sk, actual_space.index[1])
    end)
end

g.after_test('test_serialize', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that Vinyl spaces are not included into read view.
g.test_vinyl = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test', {engine = 'vinyl'})
        box.space.test:create_index('primary')
        local rv = box.read_view.open()
        t.assert_is(rv.space.test, nil)
        rv:close()
    end)
end

g.after_test('test_vinyl', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that temporary spaces are included into read view.
g.test_temporary = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test', {temporary = true})
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        local rv = box.read_view.open()
        t.assert_is_not(rv.space.test, nil)
        box.space.test:replace({1, 1})
        t.assert_equals(rv.space.test:get(1), {1})
        rv:close()
    end)
end

g.after_test('test_temporary', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that rtree indexes are not included into read view.
-- FIXME(gh-203): Include rtree indexes into read view.
g.test_rtree = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {
            type = 'rtree', unique = false, parts = {2, 'array'}
        })
        local rv = box.read_view.open()
        t.assert_is_not(rv.space.test, nil)
        t.assert_is_not(rv.space.test.index.primary, nil)
        t.assert_is(rv.space.test.index.secondary, nil)
        rv:close()
    end)
end

g.after_test('test_rtree', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that bitset indexes are not included into read view.
-- FIXME(gh-204): Include bitset indexes into read view.
g.test_bitset = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {
            type = 'bitset', unique = false, parts = {2, 'unsigned'}
        })
        local rv = box.read_view.open()
        t.assert_is_not(rv.space.test, nil)
        t.assert_is_not(rv.space.test.index.primary, nil)
        t.assert_is(rv.space.test.index.secondary, nil)
        rv:close()
    end)
end

g.after_test('test_bitset', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that tree index extents are reused after read view is closed.
g.test_gc_tree = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'tree'})
        for i = 1, 1000 do
            box.space.test:insert({i})
        end
        local mem_used_1 = box.info.memory().index
        local rv = box.read_view.open()
        for i = 1, 1000 do
            box.space.test:replace({i, i})
        end
        local mem_used_2 = box.info.memory().index
        t.assert_gt(mem_used_2, mem_used_1)
        rv:close()
        for i = 1, 2000 do
            box.space.test:replace({i, i, i})
        end
        local mem_used_3 = box.info.memory().index
        t.assert_le(mem_used_3, mem_used_2)
    end)
end

g.after_test('test_gc_tree', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that hash index extents are reused after read view is closed.
g.test_gc_hash = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'hash'})
        for i = 1, 1000 do
            box.space.test:insert({i})
        end
        local mem_used_1 = box.info.memory().index
        local rv = box.read_view.open()
        for i = 1, 1000 do
            box.space.test:replace({i, i})
        end
        local mem_used_2 = box.info.memory().index
        t.assert_gt(mem_used_2, mem_used_1)
        rv:close()
        for i = 1, 2000 do
            box.space.test:replace({i, i, i})
        end
        local mem_used_3 = box.info.memory().index
        t.assert_le(mem_used_3, mem_used_2)
    end)
end

g.after_test('test_gc_hash', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that tuple memory is freed after read view is closed.
g.test_gc_tuple = function(cg)
    cg.server:exec(function()
        local function mem_used()
            box.tuple.new() -- drop blessed tuple ref
            collectgarbage('collect') -- drop Lua tuple refs
            return box.info.memory().data
        end
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        -- Dry run to make the memtx allocator collect garbage.
        for i = 1, 100 do
            box.space.test:insert({i, string.rep('x', 10000)})
        end
        for i = 1, 100 do
            box.space.test:delete({i})
        end
        local mem_used_1 = mem_used()
        for i = 1, 100 do
            box.space.test:insert({i, string.rep('x', 10000)})
        end
        local mem_used_2 = mem_used()
        t.assert_gt(mem_used_2 - mem_used_1, 1000 * 1000)
        local rv = box.read_view.open()
        for i = 1, 100 do
            box.space.test:delete(i)
        end
        local mem_used_3 = mem_used()
        t.assert_gt(mem_used_3 - mem_used_1, 1000 * 1000)
        rv:close()
        -- Tuple garbage collection is triggered lazily, on allocation.
        for i = 1, 100 do
            box.space.test:insert({i})
            box.space.test:delete({i})
        end
        local mem_used_4 = mem_used()
        t.assert_gt(mem_used_3 - mem_used_4, 1000 * 1000)
    end)
end

g.after_test('test_gc_tuple', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that if a read view is open on garbage collection, it's closed and
-- a warning is printed to the log.
g.test_close_on_gc = function(cg)
    local rv_id, rv_name = cg.server:exec(function()
        local function mem_used()
            box.tuple.new() -- drop blessed tuple ref
            collectgarbage('collect') -- drop Lua tuple refs
            return box.info.memory().data
        end
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        -- Dry run to make the memtx allocator collect garbage.
        for i = 1, 100 do
            box.space.test:insert({i, string.rep('x', 10000)})
        end
        for i = 1, 100 do
            box.space.test:delete({i})
        end
        local mem_used_1 = mem_used()
        for i = 1, 100 do
            box.space.test:insert({i, string.rep('x', 10000)})
        end
        local mem_used_2 = mem_used()
        t.assert_gt(mem_used_2 - mem_used_1, 1000 * 1000)
        local rv = box.read_view.open({name = 'test_close_on_gc'})
        local rv_id = rv.id
        local rv_name = rv.name
        rv = nil -- luacheck: ignore
        collectgarbage('collect') -- collect the read view
        for i = 1, 100 do
            box.space.test:delete(i)
        end
        local mem_used_3 = mem_used()
        t.assert_gt(mem_used_2 - mem_used_3, 1000 * 1000)
        return rv_id, rv_name
    end)
    local fmt = "read view %d .'%s'. was not properly closed"
    t.assert(cg.server:grep_log(string.format(fmt, rv_id, rv_name)))
end

g.after_test('test_close_on_gc', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that referencing an index blocks read view garbage collection.
g.test_index_ref = function(cg)
    cg.server:exec(function()
        local weak = setmetatable({}, {__mode = 'v'})
        local rv = box.read_view.open()
        local idx = rv.space._space.index.primary._impl -- luacheck: ignore
        weak.rv = rv._impl
        rv = nil -- luacheck: ignore
        for _ = 1, 5 do collectgarbage('collect') end
        t.assert_is_not(weak.rv, nil)
        idx = nil
        for _ = 1, 5 do collectgarbage('collect') end
        t.assert_is(weak.rv, nil)
    end)
end

-- Checks that referencing an iterator blocks read view garbage collection.
g.test_iterator_ref = function(cg)
    cg.server:exec(function()
        local weak = setmetatable({}, {__mode = 'v'})
        local rv = box.read_view.open()
        local idx = rv.space._space.index.primary
        local gen, param, state = idx:pairs() -- luacheck: ignore
        weak.rv = rv._impl
        rv = nil -- luacheck: ignore
        idx = nil -- luacheck: ignore
        for _ = 1, 5 do collectgarbage('collect') end
        t.assert_is_not(weak.rv, nil)
        gen, param, state = nil, nil, nil
        for _ = 1, 5 do collectgarbage('collect') end
        t.assert_is(weak.rv, nil)
    end)
end

-- Checks that an error is raised if close is called for the second time.
g.test_double_close = function(cg)
    cg.server:exec(function()
        local rv = box.read_view.open()
        rv:close()
        local err = {type = 'ClientError', name = 'READ_VIEW_CLOSED'}
        t.assert_error_covers(err, rv.close, rv)
    end)
end

-- Checks errors raised when 'get' is used with invalid arguments.
g.test_get_invalid = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {
            parts = {1, 'unsigned', 2, 'string'},
        })
        box.space.test:create_index('secondary', {
            unique = false, parts = {3, 'unsigned'},
        })
        local rv = box.read_view.open()
        local s = rv.space.test
        t.assert_error_msg_equals(
            "Invalid key part count in an exact match (expected 2, got 1)",
            s.index.primary.get, s.index.primary, {1})
        t.assert_error_msg_equals(
            "Supplied key type of part 1 does not match index part type: " ..
            "expected string",
            s.index.primary.get, s.index.primary, {1, 2})
        t.assert_error_msg_equals(
            "Get() doesn't support partial keys and non-unique indexes",
            s.index.secondary.get, s.index.secondary, {1})
        rv:close()
    end)
end

g.after_test('test_get_invalid', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that an error is raised if 'get' is used on a closed read view.
g.test_get_after_close = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        rv:close()
        local err = {type = 'ClientError', name = 'READ_VIEW_CLOSED'}
        t.assert_error_covers(err, idx.get, idx, {1})
        t.assert_error_covers(err, idx.get, idx, {2})
    end)
end

g.after_test('test_get_after_close', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'get' implementation of the tree index read view.
g.test_get_tree = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'tree'})
        box.space.test:create_index('secondary', {
            type = 'tree', parts = {2, 'string', 3, 'unsigned'},
        })
        box.space.test:insert{1, 'foo', 10}
        box.space.test:insert{2, 'bar', 20}
        box.space.test:insert{3, 'baz', 30}
        local rv = box.read_view.open()
        local s = rv.space.test
        box.space.test:replace{2, 'BAR', 200}
        box.space.test:delete({3})
        t.assert_equals(box.space.test:get({1}), {1, 'foo', 10})
        t.assert_equals(box.space.test:get({2}), {2, 'BAR', 200})
        t.assert_equals(box.space.test:get({3}), nil)
        t.assert_equals(s.index.primary:get({1}), {1, 'foo', 10})
        t.assert_equals(s.index.primary:get({2}), {2, 'bar', 20})
        t.assert_equals(s.index.primary:get({3}), {3, 'baz', 30})
        t.assert_equals(s.index.secondary:get({'foo', 10}), {1, 'foo', 10})
        t.assert_equals(s.index.secondary:get({'bar', 20}), {2, 'bar', 20})
        t.assert_equals(s.index.secondary:get({'baz', 30}), {3, 'baz', 30})
        t.assert_equals(s.index.primary:get(1), {1, 'foo', 10})
        t.assert_equals(s.index.secondary:get(box.tuple.new{'foo', 10}),
                        {1, 'foo', 10})
        t.assert_is(s.index.primary:get({4}), nil)
        t.assert_is(s.index.secondary:get({'baz', 10}), nil)
        rv:close()
    end)
end

g.after_test('test_get_tree', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'get' implementation of the hash index read view.
g.test_get_hash = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'hash'})
        box.space.test:create_index('secondary', {
            type = 'hash', parts = {2, 'string', 3, 'unsigned'},
        })
        box.space.test:insert{1, 'foo', 10}
        box.space.test:insert{2, 'bar', 20}
        box.space.test:insert{3, 'baz', 30}
        local rv = box.read_view.open()
        local s = rv.space.test
        box.space.test:replace{2, 'BAR', 200}
        box.space.test:delete({3})
        t.assert_equals(box.space.test:get({1}), {1, 'foo', 10})
        t.assert_equals(box.space.test:get({2}), {2, 'BAR', 200})
        t.assert_equals(box.space.test:get({3}), nil)
        t.assert_equals(s.index.primary:get({1}), {1, 'foo', 10})
        t.assert_equals(s.index.primary:get({2}), {2, 'bar', 20})
        t.assert_equals(s.index.primary:get({3}), {3, 'baz', 30})
        t.assert_equals(s.index.secondary:get({'foo', 10}), {1, 'foo', 10})
        t.assert_equals(s.index.secondary:get({'bar', 20}), {2, 'bar', 20})
        t.assert_equals(s.index.secondary:get({'baz', 30}), {3, 'baz', 30})
        t.assert_equals(s.index.primary:get(1), {1, 'foo', 10})
        t.assert_equals(s.index.secondary:get(box.tuple.new{'foo', 10}),
                        {1, 'foo', 10})
        t.assert_is(s.index.primary:get({4}), nil)
        t.assert_is(s.index.secondary:get({'baz', 10}), nil)
        rv:close()
    end)
end

g.after_test('test_get_hash', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'get' implementation of the multikey index read view.
g.test_get_multikey = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {
            parts = {{'[2][*]', 'unsigned'}}
        })
        box.space.test:insert({1, {10}})
        box.space.test:insert({2, {20, 200}})
        box.space.test:insert({3, {300, 30, 3000}})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.secondary
        box.space.test:replace({1, {10, 100}})
        box.space.test:replace({2, {20}})
        box.space.test:delete({3})
        t.assert_equals(box.space.test.index.secondary:get(10), {1, {10, 100}})
        t.assert_equals(box.space.test.index.secondary:get(100), {1, {10, 100}})
        t.assert_equals(box.space.test.index.secondary:get(20), {2, {20}})
        t.assert_equals(box.space.test.index.secondary:get(200), nil)
        t.assert_equals(box.space.test.index.secondary:get(30), nil)
        t.assert_equals(box.space.test.index.secondary:get(300), nil)
        t.assert_equals(idx:get(10), {1, {10}})
        t.assert_equals(idx:get(100), nil)
        t.assert_equals(idx:get(20), {2, {20, 200}})
        t.assert_equals(idx:get(200), {2, {20, 200}})
        t.assert_equals(idx:get(2000), nil)
        t.assert_equals(idx:get(30), {3, {300, 30, 3000}})
        t.assert_equals(idx:get(300), {3, {300, 30, 3000}})
        t.assert_equals(idx:get(3000), {3, {300, 30, 3000}})
        rv:close()
    end)
end

g.after_test('test_get_multikey', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'get' implementation of the functional index read view.
g.test_get_func = function(cg)
    cg.server:exec(function()
        box.schema.func.create('test', {
            body = [[function(t) return {t[1] + t[2]} end]],
            is_deterministic = true,
            is_sandboxed = true,
        })
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {
            parts = {1, 'unsigned'},
            func = box.func.test.id,
        })
        box.space.test:insert({1, 1})
        box.space.test:insert({2, 2})
        box.space.test:insert({3, 3})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.secondary
        box.space.test:replace({1, 2})
        box.space.test:replace({2, 3})
        box.space.test:delete({3})
        t.assert_equals(box.space.test.index.secondary:get(1), nil)
        t.assert_equals(box.space.test.index.secondary:get(2), nil)
        t.assert_equals(box.space.test.index.secondary:get(3), {1, 2})
        t.assert_equals(box.space.test.index.secondary:get(4), nil)
        t.assert_equals(box.space.test.index.secondary:get(5), {2, 3})
        t.assert_equals(box.space.test.index.secondary:get(6), nil)
        t.assert_equals(idx:get(1), nil)
        t.assert_equals(idx:get(2), {1, 1})
        t.assert_equals(idx:get(3), nil)
        t.assert_equals(idx:get(4), {2, 2})
        t.assert_equals(idx:get(5), nil)
        t.assert_equals(idx:get(6), {3, 3})
        rv:close()
    end)
end

g.after_test('test_get_func', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

-- Checks 'get' implementation of the sequence data index read view.
g.test_get_sequence = function(cg)
    cg.server:exec(function()
        local seq = box.schema.sequence.create('test')
        seq:next()
        t.assert_equals(box.space._sequence_data:get(seq.id), {seq.id, 1})
        local rv = box.read_view.open()
        local idx = rv.space._sequence_data.index.primary
        t.assert_error_msg_equals(
            "_sequence_data read view does not support get()",
            idx.get, idx, 1)
        rv:close()
    end)
end

g.after_test('test_get_sequence', function(cg)
    cg.server:exec(function()
        box.schema.sequence.drop('test', {if_exists = true})
    end)
end)

-- Checks errors raised when 'select' is used with invalid arguments.
g.test_select_invalid = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        t.assert_error_msg_equals(
            "Unknown iterator type 'foo'",
            idx.select, idx, {}, {iterator = 'foo'})
        t.assert_error_msg_equals(
            "Invalid iterator type", idx.select, idx, {}, {iterator = 42})
        t.assert_error_msg_equals(
            "Supplied key type of part 0 does not match index part type: " ..
            "expected unsigned",
            idx.select, idx, {'foo'})
        rv:close()
    end)
end

g.after_test('test_select_invalid', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that an error is raised if 'select' is used on a closed read view.
g.test_select_after_close = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        rv:close()
        local err = {type = 'ClientError', name = 'READ_VIEW_CLOSED'}
        t.assert_error_covers(err, idx.select, idx, {1})
        t.assert_error_covers(err, idx.select, idx, {2})
    end)
end

g.after_test('test_select_after_close', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks errors raised when 'select' is used with arguments that are not
-- supported by a tree index.
g.test_select_tree_invalid = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'tree'})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        for _, it in pairs({
            'bits_all_set', 'bits_any_set', 'bits_all_not_set',
            'overlaps', 'neighbor', 'pp', 'np',
            box.index.BITS_ALL_SET, box.index.BITS_ANY_SET,
            box.index.BITS_ALL_NOT_SET, box.index.OVERLAPS, box.index.NEIGHBOR,
        }) do
            local err = "Index 'primary' (TREE) of space 'test' (memtx) " ..
                        "does not support requested iterator type"
            t.assert_error_msg_equals(err, idx.select, idx, {},
                                      {iterator = it})
            t.assert_error_msg_equals(err, idx.count, idx, {},
                                      {iterator = it})
        end
        rv:close()
    end)
end

g.after_test('test_select_tree_invalid', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'select' implementation of the tree index read view.
g.test_select_tree = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'tree'})
        box.space.test:create_index('secondary', {
            type = 'tree', parts = {2, 'string', 3, 'unsigned'},
        })
        box.space.test:insert{1, 'aaa', 10}
        box.space.test:insert{2, 'aaa', 20}
        box.space.test:insert{3, 'bbb', 30}
        box.space.test:insert{4, 'bbb', 40}
        box.space.test:insert{5, 'ccc', 50}
        box.space.test:insert{6, 'ccc', 60}
        local rv = box.read_view.open()
        local idx1 = rv.space.test.index.primary
        local idx2 = rv.space.test.index.secondary
        box.space.test:replace{1, 'aaa', 100}
        box.space.test:replace{2, 'bbb', 200}
        box.space.test:delete({3})
        t.assert_equals(box.space.test:select(), {
            {1, 'aaa', 100}, {2, 'bbb', 200},
            {4, 'bbb', 40}, {5, 'ccc', 50}, {6, 'ccc', 60},
        })
        local expected = {
            {1, 'aaa', 10}, {2, 'aaa', 20}, {3, 'bbb', 30},
            {4, 'bbb', 40}, {5, 'ccc', 50}, {6, 'ccc', 60},
        }
        t.assert_equals(idx1:select(), expected)
        t.assert_equals(idx2:select(), expected)
        for _, it in pairs({
            'gt', 'ge', 'eq', 'all',
            box.index.GT, box.index.GE, box.index.EQ, box.index.ALL,
        }) do
            t.assert_equals(idx1:select({}, {iterator = it}), expected)
            t.assert_equals(idx2:select({}, {iterator = it}), expected)
        end
        for i = 1, #expected / 2 do
            local tmp = expected[#expected - i + 1]
            expected[#expected - i + 1] = expected[i]
            expected[i] = tmp
        end
        for _, it in pairs({
            'lt', 'le', 'req', box.index.LT, box.index.LE, box.index.REQ,
        }) do
            t.assert_equals(idx1:select({}, {iterator = it}), expected)
            t.assert_equals(idx2:select({}, {iterator = it}), expected)
        end
        t.assert_equals(idx1:select({0}, {iterator = 'req'}), {})
        t.assert_equals(idx1:select({0}, {iterator = 'le'}), {})
        t.assert_equals(idx1:select({1}, {iterator = 'lt'}), {})
        t.assert_equals(idx1:select({1}), {{1, 'aaa', 10}})
        t.assert_equals(idx1:select({1}, {iterator = 'eq'}), {{1, 'aaa', 10}})
        t.assert_equals(idx1:select({2}, {iterator = 'req'}), {{2, 'aaa', 20}})
        t.assert_equals(idx1:select({2}, {iterator = 'le'}),
                        {{2, 'aaa', 20}, {1, 'aaa', 10}})
        t.assert_equals(idx1:select({3}, {iterator = 'lt'}),
                        {{2, 'aaa', 20}, {1, 'aaa', 10}})
        t.assert_equals(idx1:select({4}, {iterator = 'gt'}),
                        {{5, 'ccc', 50}, {6, 'ccc', 60}})
        t.assert_equals(idx1:select({5}, {iterator = 'ge'}),
                        {{5, 'ccc', 50}, {6, 'ccc', 60}})
        t.assert_equals(idx1:select({6}, {iterator = 'gt'}), {})
        t.assert_equals(idx1:select({7}, {iterator = 'ge'}), {})
        t.assert_equals(idx1:select({7}, {iterator = 'eq'}), {})
        t.assert_equals(idx1:select({7}), {})
        t.assert_equals(idx2:select({'aaa'}), {{1, 'aaa', 10}, {2, 'aaa', 20}})
        t.assert_equals(idx2:select({'aaa'}, {iterator = 'eq'}),
                        {{1, 'aaa', 10}, {2, 'aaa', 20}})
        t.assert_equals(idx2:select({'aaa'}, {iterator = 'req'}),
                        {{2, 'aaa', 20}, {1, 'aaa', 10}})
        t.assert_equals(idx2:select({'bbb'}, {iterator = 'le'}), {
            {4, 'bbb', 40}, {3, 'bbb', 30}, {2, 'aaa', 20}, {1, 'aaa', 10}
        })
        t.assert_equals(idx2:select({'ccc'}, {iterator = 'lt'}), {
            {4, 'bbb', 40}, {3, 'bbb', 30}, {2, 'aaa', 20}, {1, 'aaa', 10}
        })
        t.assert_equals(idx2:select({'aaa'}, {iterator = 'gt'}), {
            {3, 'bbb', 30}, {4, 'bbb', 40}, {5, 'ccc', 50}, {6, 'ccc', 60}
        })
        t.assert_equals(idx2:select({'bbb'}, {iterator = 'ge'}), {
            {3, 'bbb', 30}, {4, 'bbb', 40}, {5, 'ccc', 50}, {6, 'ccc', 60}
        })
        t.assert_equals(idx1:select(1), {{1, 'aaa', 10}})
        t.assert_equals(idx2:select('aaa'), {{1, 'aaa', 10}, {2, 'aaa', 20}})
        t.assert_equals(idx1:select(box.tuple.new(1)), {{1, 'aaa', 10}})
        t.assert_equals(idx2:select(box.tuple.new{'aaa', 10}), {{1, 'aaa', 10}})
        rv:close()
    end)
end

g.after_test('test_select_tree', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks errors raised when 'select' is used with arguments that are not
-- supported by a hash index.
g.test_select_hash_invalid = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {
            type = 'hash', parts = {1, 'unsigned', 2, 'unsigned'},
        })
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        for _, it in pairs({
            'req', 'lt', 'le', 'ge',
            'bits_all_set', 'bits_any_set', 'bits_all_not_set',
            'overlaps', 'neighbor', 'pp', 'np',
            box.index.REQ, box.index.LT, box.index.LE, box.index.GE,
            box.index.BITS_ALL_SET, box.index.BITS_ANY_SET,
            box.index.BITS_ALL_NOT_SET, box.index.OVERLAPS, box.index.NEIGHBOR,
        }) do
            local err = "Index 'primary' (HASH) of space 'test' (memtx) " ..
                        "does not support requested iterator type"
            t.assert_error_msg_equals(err, idx.select, idx, {1, 1},
                                      {iterator = it})
            t.assert_error_msg_equals(err, idx.count, idx, {1, 1},
                                      {iterator = it})
        end
        t.assert_error_msg_equals(
            "HASH index  does not support selects via a partial key " ..
            "(expected 2 parts, got 1). " ..
            "Please Consider changing index type to TREE.",
            idx.select, idx, {1})
        rv:close()
    end)
end

g.after_test('test_select_hash_invalid', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'select' implementation of the hash index read view.
g.test_select_hash = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'hash'})
        box.space.test:insert{1, 10}
        box.space.test:insert{2, 20}
        box.space.test:insert{3, 30}
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        box.space.test:replace{1, 100}
        box.space.test:delete({2})
        t.assert_items_equals(box.space.test:select(), {{1, 100}, {3, 30}})
        local expected = {{1, 10}, {2, 20}, {3, 30}}
        t.assert_items_equals(idx:select(), expected)
        t.assert_items_equals(idx:select({}, {iterator = 'gt'}), expected)
        t.assert_items_equals(idx:select({}, {iterator = 'all'}), expected)
        t.assert_equals(idx:select({1}), {{1, 10}})
        t.assert_equals(idx:select({1}, {iterator = 'eq'}), {{1, 10}})
        t.assert_equals(idx:select({4}, {iterator = 'eq'}), {})
        t.assert_equals(idx:select(1), {{1, 10}})
        t.assert_equals(idx:select(box.tuple.new(1)), {{1, 10}})
        rv:close()
    end)
end

g.after_test('test_select_hash', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'select' implementation of the multikey index read view.
g.test_select_multikey = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {
            unique = false, parts = {{'[2][*]', 'unsigned'}}
        })
        box.space.test:insert({1, {10}})
        box.space.test:insert({2, {10, 20}})
        box.space.test:insert({3, {30, 20}})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.secondary
        box.space.test:replace({1, {10, 100}})
        box.space.test:replace({2, {20}})
        box.space.test:delete({3})
        t.assert_equals(box.space.test.index.secondary:select(),
                        {{1, {10, 100}}, {2, {20}}, {1, {10, 100}}})
        t.assert_equals(idx:select(10, 'lt'), {})
        t.assert_equals(idx:select(10), {{1, {10}}, {2, {10, 20}}})
        t.assert_equals(idx:select(10, 'gt'),
                        {{2, {10, 20}}, {3, {30, 20}}, {3, {30, 20}}})
        t.assert_equals(idx:select(30, 'ge'), {{3, {30, 20}}})
        rv:close()
    end)
end

g.after_test('test_select_multikey', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks 'select' implementation of the functional index read view.
g.test_select_func = function(cg)
    cg.server:exec(function()
        box.schema.func.create('test', {
            body = [[function(t) return {t[1] + t[2]} end]],
            is_deterministic = true,
            is_sandboxed = true,
        })
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {
            unique = false,
            parts = {1, 'unsigned'},
            func = box.func.test.id,
        })
        box.space.test:insert({1, 2})
        box.space.test:insert({2, 1})
        box.space.test:insert({3, 3})
        box.space.test:insert({4, 2})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.secondary
        box.space.test:replace({1, 1})
        box.space.test:replace({2, 2})
        box.space.test:replace({3, 1})
        box.space.test:delete({4})
        t.assert_equals(box.space.test.index.secondary:select(2), {{1, 1}})
        t.assert_equals(box.space.test.index.secondary:select(3), {})
        t.assert_equals(box.space.test.index.secondary:select(4),
                        {{2, 2}, {3, 1}})
        t.assert_equals(box.space.test.index.secondary:select(5), {})
        t.assert_equals(box.space.test.index.secondary:select(6), {})
        t.assert_equals(idx:select(2), {})
        t.assert_equals(idx:select(3), {{1, 2}, {2, 1}})
        t.assert_equals(idx:select(4), {})
        t.assert_equals(idx:select(5), {})
        t.assert_equals(idx:select(6), {{3, 3}, {4, 2}})
        rv:close()
    end)
end

g.after_test('test_select_func', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

-- Checks 'select' implementation of the sequence data index read view.
g.test_select_sequence = function(cg)
    cg.server:exec(function()
        local seq1 = box.schema.sequence.create('test1')
        local seq2 = box.schema.sequence.create('test2')
        seq1:next()
        seq2:next()
        seq2:next()
        t.assert_items_equals(box.space._sequence_data:select(),
                              {{seq1.id, 1}, {seq2.id, 2}})
        local rv = box.read_view.open()
        local idx = rv.space._sequence_data.index.primary
        seq1:next()
        seq2:next()
        t.assert_items_equals(box.space._sequence_data:select(),
                              {{seq1.id, 2}, {seq2.id, 3}})
        t.assert_items_equals(idx:select(), {{seq1.id, 1}, {seq2.id, 2}})
        t.assert_error_msg_equals(
            "_sequence_data read view does not support requested iterator type",
            idx.select, idx, seq1.id)
        rv:close()
    end)
end

g.after_test('test_select_sequence', function(cg)
    cg.server:exec(function()
        box.schema.sequence.drop('test1', {if_exists = true})
        box.schema.sequence.drop('test2', {if_exists = true})
    end)
end)

-- Checks 'offset' and 'limit' options of the 'select' index read view method.
g.test_select_offset_limit = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        for i = 1, 5 do
            box.space.test:insert({i})
        end
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        t.assert_equals(idx:select({}, {limit = 0}), {})
        t.assert_equals(idx:select({}, {limit = 1}), {{1}})
        t.assert_equals(idx:select({}, {limit = 3}), {{1}, {2}, {3}})
        t.assert_equals(idx:select({}, {offset = 0}), {{1}, {2}, {3}, {4}, {5}})
        t.assert_equals(idx:select({}, {offset = 2}), {{3}, {4}, {5}})
        t.assert_equals(idx:select({}, {offset = 4}), {{5}})
        t.assert_equals(idx:select({}, {offset = 5}), {})
        t.assert_equals(idx:select({2}, {
            iterator = 'ge', offset = 1, limit = 2
        }), {{3}, {4}})
        t.assert_equals(idx:select({10}, {
            iterator = 'le', offset = 2, limit = 2
        }), {{3}, {2}})
        t.assert_equals(idx:select({10}, {
            iterator = 'lt', offset = 2, limit = 5
        }), {{3}, {2}, {1}})
        t.assert_equals(idx:select({10}, {
            iterator = 'gt', offset = 2, limit = 5
        }), {})
        rv:close()
    end)
end

g.after_test('test_select_offset_limit', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks the 'offset' select and pairs option thoroughly.
g.test_offset_result = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- A space with a secondary key (so we can check nulls).
        local s = box.schema.space.create('test')
        s:create_index('pk')

        -- The tested index and a read view.
        local sk = s:create_index('sk',
                                  {parts = {{2, 'uint64', is_nullable = true},
                                            {3, 'uint64', is_nullable = true}}})
        local rv = box.read_view.open()

        -- The test data.

        local existing_tuples = {
            {1, 1, 1},
            {2, 1, 3},
            {3, 3, 1},
            {4, 3, 2},
            {5, 5, 1},
            {6, 5, 3},
        }

        local test_keys = {
            {},
            {box.NULL}, {box.NULL, box.NULL}, {box.NULL, 0},
            {box.NULL, 1}, {box.NULL, 2}, {box.NULL, 3}, {box.NULL, 4},
            {0}, {0, box.NULL}, {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4},
            {1}, {1, box.NULL}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4},
            {2}, {2, box.NULL}, {2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4},
            {3}, {3, box.NULL}, {3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4},
            {4}, {4, box.NULL}, {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4},
            {5}, {5, box.NULL}, {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4},
            {6}, {6, box.NULL}, {6, 0}, {6, 1}, {6, 2}, {6, 3}, {6, 4},
        }

        local test_offsets = {0, 1, 2, 3, 10}

        local all_iterators = {'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}

        -- A helper function for verbose assertion using pretty printer.
        local function check(it, key, offset, expect)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = pp.tostring(key)

            local comment = string.format(
                '\nkey: %s,\noffset = %d,\niterator: %s,\nfile: %s,' ..
                '\nline: %d,', key_str, offset, it, file, line)

            local opts = {iterator = it, offset = offset}

            local result = rv.space.test.index.sk:select(key, opts)
            t.assert_equals(result, expect, comment)

            local pairs_result = {}
            for _, tuple in rv.space.test.index.sk:pairs(key, opts) do
                table.insert(pairs_result, tuple)
            end
            t.assert_equals(pairs_result, expect, comment)
        end

        -- Test the empty space.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                for _, offset in pairs(test_offsets) do
                    check(it, key, offset, {})
                    -- The test is pretty long, need to yield once for
                    -- a while to not trigger the fiber slice check.
                    fiber.yield()
                end
            end
        end

        -- Fill the space.
        for _, tuple in pairs(existing_tuples) do
            s:insert(tuple)
        end

        -- Recreate the read view.
        rv:close()
        rv = box.read_view.open()

        -- Test the non-empty space.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = sk:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    check(it, key, offset, expect)
                    -- The test is pretty long, need to yield once for
                    -- a while to not trigger the fiber slice check.
                    fiber.yield()
                end
            end
        end

        -- Clean-up.
        rv:close()
    end)
end

g.after_test('test_offset_result', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks the read view 'count' implementation.
g.test_count_result = function(cg)
    cg.server:exec(function()
        -- A space with a secondary key (so we can check nulls).
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'uint64', is_nullable = true},
                                       {3, 'uint64', is_nullable = true}}})
        local rv = box.read_view.open()

        -- A helper function for verbose assertion using pretty printer.
        local function check(it, key, expected_count)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = pp.tostring(key)

            t.assert_equals(rv.space.test.index.sk:count(key, {iterator = it}),
                            expected_count, string.format(
                            '\nkey: %s,\niterator: %s,\nfile: %s,\nline: %d,',
                            key_str, it, file, line))
        end

        -- Test the empty space.
        for _, it in pairs({'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}) do
            check(it, {}, 0)
            check(it, {box.NULL}, 0)
            check(it, {0}, 0)
            check(it, {1}, 0)
            check(it, {1, box.NULL}, 0)
            check(it, {1, 0}, 0)
            check(it, {1, 1}, 0)
        end

        -- Fill the space.
        s:insert({1, 1, 1})
        s:insert({2, 1, 2})
        s:insert({3, 2, 1})
        s:insert({4, 2, 2})
        s:insert({5, 3, 1})
        s:insert({6, 3, 2})
        t.assert_equals(s:count(), 6)

        -- Recreate the read view.
        rv:close()
        rv = box.read_view.open()

        -- Empty key always returns the space size.
        for _, it in pairs({'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}) do
            check(it, {}, s:count())
        end

        -- GE, ALL (it's identical to GE according to the documentation).
        for _, it in pairs({'ge', 'all'}) do
            check(it, {box.NULL}, 6)
            check(it, {1}, 6)
            check(it, {1, 1}, 6)
            check(it, {1, 2}, 5)
            check(it, {1, 3}, 4)
            check(it, {2}, 4)
            check(it, {2, box.NULL}, 4)
            check(it, {2, 1}, 4)
            check(it, {2, 2}, 3)
            check(it, {3, 1}, 2)
            check(it, {3, 2}, 1)
            check(it, {3, 3}, 0)
            check(it, {4}, 0)
        end

        -- GT.
        check('gt', {box.NULL}, 6)
        check('gt', {1}, 4)
        check('gt', {2}, 2)
        check('gt', {2, 1}, 3)
        check('gt', {2, 2}, 2)
        check('gt', {2, box.NULL}, 4)
        check('gt', {3, 1}, 1)
        check('gt', {3, 2}, 0)
        check('gt', {3, 3}, 0)
        check('gt', {3}, 0)

        -- LE.
        check('le', {3}, 6)
        check('le', {3, 2}, 6)
        check('le', {3, 1}, 5)
        check('le', {3, box.NULL}, 4)
        check('le', {2}, 4)
        check('le', {2, 2}, 4)
        check('le', {2, 1}, 3)
        check('le', {2, box.NULL}, 2)
        check('le', {1}, 2)
        check('le', {0}, 0)
        check('le', {box.NULL}, 0)

        -- LT.
        check('lt', {4}, 6)
        check('lt', {3, 3}, 6)
        check('lt', {3, 2}, 5)
        check('lt', {3, 1}, 4)
        check('lt', {3}, 4)
        check('lt', {2, 2}, 3)
        check('lt', {2, 1}, 2)
        check('lt', {2}, 2)
        check('lt', {2, box.NULL}, 2)
        check('lt', {1}, 0)
        check('lt', {0}, 0)
        check('lt', {box.NULL}, 0)

        -- EQ/REQ.
        for _, it in pairs({'eq', 'req'}) do
            check(it, {box.NULL}, 0)
            for _, key in pairs({1, 2, 3}) do
                check(it, {key}, 2)
                check(it, {key, 1}, 1)
                check(it, {key, 2}, 1)
                check(it, {key, box.NULL}, 0)
            end
        end

        -- Clean-up.
        rv:close()
    end)
end

g.after_test('test_count_result', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks the read view 'offset_of' implementation.
g.test_offset_of = function(cg)
    cg.server:exec(function()
        -- Create and fill the space.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert({1})
        s:insert({3})
        local rv = box.read_view.open()
        local rv_pk = rv.space.test.index.pk

        -- A test wrapper to fit cases in single lines.
        local function check(key, it, expect)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = pp.tostring(key)

            t.assert_equals(rv_pk:offset_of(key, {iterator = it}), expect,
                            string.format('\nkey: %s,\niterator: %s,' ..
                                          '\nfile: %s,\nline: %d,',
                                          key_str, it, file, line))
        end

        -- iterator = GE
        check({0}, 'ge', 0) -- [<1>, 3]
        check({1}, 'ge', 0) -- [<1>, 3]
        check({2}, 'ge', 1) -- [1, <3>]
        check({3}, 'ge', 1) -- [1, <3>]
        check({4}, 'ge', 2) -- [1, 3, <...>]

        -- iterator = GT
        check({0}, 'gt', 0) -- [<1>, 3]
        check({1}, 'gt', 1) -- [1, <3>]
        check({2}, 'gt', 1) -- [1, <3>]
        check({3}, 'gt', 2) -- [1, 3, <...>]
        check({4}, 'gt', 2) -- [1, 3, <...>]

        -- iterator = LE
        check({0}, 'le', 2) -- [3, 1, <...>]
        check({1}, 'le', 1) -- [3, <1>]
        check({2}, 'le', 1) -- [3, <1>]
        check({3}, 'le', 0) -- [<3>, 1]
        check({4}, 'le', 0) -- [<3>, 1]

        -- iterator = LT
        check({0}, 'lt', 2) -- [3, 1, <...>]
        check({1}, 'lt', 2) -- [3, 1, <...>]
        check({2}, 'lt', 1) -- [3, <1>]
        check({3}, 'lt', 1) -- [3, <1>]
        check({4}, 'lt', 0) -- [<3>, 1]

        -- iterator = EQ
        check({0}, 'eq', 0) -- [<0>, 1, 3]
        check({1}, 'eq', 0) -- [<1>, 3]
        check({2}, 'eq', 1) -- [1, <2>, 3]
        check({3}, 'eq', 1) -- [1, <3>]
        check({4}, 'eq', 2) -- [1, 3, <4>]

        -- iterator = REQ
        check({0}, 'req', 2) -- [3, 1, <0>]
        check({1}, 'req', 1) -- [3, <1>]
        check({2}, 'req', 1) -- [3, <2>, 1]
        check({3}, 'req', 0) -- [<3>, 1]
        check({4}, 'req', 0) -- [<4>, 3, 1]

        -- Create and fill a multipart index.
        s.index.pk:drop()
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        s:insert({1, 1})
        s:insert({1, 2})
        s:insert({2, 1})

        -- Recreate the read view.
        rv:close()
        rv = box.read_view.open()
        rv_pk = rv.space.test.index.pk

        check({1}, 'eq',  0) -- [<{1, 1}>, {1, 2}, {2, 1}]
        check({1}, 'gt',  2) -- [{1, 1}, {1, 2}, <{2, 1}>]
        check({1}, 'ge',  0) -- [<{1, 1}>, {1, 2}, {2, 1}]
        check({1}, 'req', 1) -- [{2, 1}, <{1, 2}>, {1, 1}]
        check({1}, 'lt',  3) -- [{2, 1}, {1, 2}, {1, 1}, <...>]
        check({1}, 'le',  1) -- [{2, 1}, <{1, 2}>, {1, 1}]

        -- Clean-up.
        rv:close()
    end)
end

g.after_test('test_offset_of', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks the 'offset_of' function parameters.
g.test_offset_of_params = function(cg)
    cg.server:exec(function()
        -- Create and fill a space.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        s:insert({1, 1})
        s:insert({1, 2})
        s:insert({2, 1})
        s:insert({2, 2})
        s:insert({3, 1})
        s:insert({3, 2})

        -- Create a read view.
        local rv = box.read_view.open()
        local srv = rv.space.test

        -- No arguments.
        t.assert_equals(srv:offset_of(), 0)

        -- Empty key.
        t.assert_equals(srv:offset_of({}), 0)

        -- Numeric key.
        t.assert_equals(srv:offset_of(2), 2)

        -- Regular key.
        t.assert_equals(srv:offset_of({2}), 2)

        -- Empty opts.
        t.assert_equals(srv:offset_of({2}, {}), 2)

        -- String iterator.
        t.assert_equals(srv:offset_of({2}, {iterator = 'eq'}), 2)

        -- Number iterator.
        t.assert_equals(srv:offset_of({2}, {iterator = box.index.EQ}), 2)

        -- Invalid iterator.
        t.assert_error_msg_contains('Unknown iterator type',
                                    srv.offset_of, srv, {2}, {iterator = "bad"})

        -- Invalid iterator type.
        t.assert_error_msg_contains('Unknown iterator type',
                                    srv.offset_of, srv, {2}, {iterator = true})

        -- String opts.
        t.assert_equals(srv:offset_of({2}, 'eq'), 2)

        -- Number opts.
        t.assert_equals(srv:offset_of({2}, box.index.EQ), 2)

        -- Invalid opts.
        t.assert_error_msg_contains('Unknown iterator type',
                                    srv.offset_of, srv, {2}, 'bad')

        -- Invalid opts type.
        t.assert_error_msg_contains('Unknown iterator type',
                                    srv.offset_of, srv, {2}, true)

        -- Clean-up.
        rv:close()
    end)
end

g.after_test('test_offset_of_params', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks errors raised when 'pairs' is used with invalid arguments.
g.test_pairs_invalid = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        t.assert_error_msg_equals(
            "Unknown iterator type 'foo'",
            idx.pairs, idx, {}, {iterator = 'foo'})
        t.assert_error_msg_equals(
            "Invalid iterator type", idx.pairs, idx, {}, {iterator = 42})
        t.assert_error_msg_equals(
            "Supplied key type of part 0 does not match index part type: " ..
            "expected unsigned",
            idx.pairs, idx, {'foo'})
        rv:close()
    end)
end

g.after_test('test_pairs_invalid', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that an error is raised if 'pairs' is used on a closed read view.
g.test_pairs_after_close = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        box.space.test:insert({2})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        local gen, param, state = idx:pairs()
        rv:close()
        local err = {type = 'ClientError', name = 'READ_VIEW_CLOSED'}
        t.assert_error_covers(err, idx.pairs, idx)
        t.assert_error_covers(err, gen, param, state)
    end)
end

g.after_test('test_pairs_after_close', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks the 'pairs' method of an index read view.
g.test_pairs = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        for i = 1, 5 do
            box.space.test:insert({i * 10})
        end
        local rv = box.read_view.open()
        for i = 1, 5 do
            box.space.test:insert({i * 10 + 1})
        end
        local idx = rv.space.test.index.primary
        local function do_pairs(...)
            local result = {}
            for k, v in idx:pairs(...) do
                result[k] = v
            end
            return result
        end
        t.assert_equals(do_pairs(), {{10}, {20}, {30}, {40}, {50}})
        t.assert_equals(do_pairs({}, 'req'), {{50}, {40}, {30}, {20}, {10}})
        t.assert_equals(do_pairs({25}, 'gt'), {{30}, {40}, {50}})
        t.assert_equals(do_pairs({30}, 'lt'), {{20}, {10}})
        t.assert_equals(do_pairs({55}, 'ge'), {})
        t.assert_equals(do_pairs(box.tuple.new(20), 'le'), {{20}, {10}})
        t.assert_equals(do_pairs(20, 'lt'), {{10}})
        t.assert_equals(do_pairs(20), {{20}})
        rv:close()
    end)
end

g.after_test('test_pairs', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_quantile_invalid = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {
            parts = {1, 'unsigned', 2, 'unsigned'},
        })
        box.space.test:create_index('hash', {type = 'hash'})
        local rv = box.read_view.open()
        local i = rv.space.test.index.primary
        local err = {
            type = 'IllegalParams',
            message = 'Usage: index:quantile(level[, begin_key, end_key])',
        }
        t.assert_error_covers(err, i.quantile, i)
        err = {
            type = 'IllegalParams',
            message = 'level must be a number',
        }
        t.assert_error_covers(err, i.quantile, i, 'foo')
        err = {
            type = 'IllegalParams',
            message = 'level must be > 0 and < 1',
        }
        t.assert_error_covers(err, i.quantile, i, 0)
        t.assert_error_covers(err, i.quantile, i, 1)
        t.assert_error_covers(err, i.quantile, i, 1.5)

        local key = function() end
        err = {
            type = 'ClientError',
            name = 'PROC_LUA',
            message = "can not encode Lua type: 'function'",
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, key)
        t.assert_error_covers(err, i.quantile, i, 0.5, nil, key)

        key = {'foo', 'bar'}
        err = {
            type = 'ClientError',
            name = 'KEY_PART_TYPE',
            message = 'Supplied key type of part 0 does not match ' ..
                      'index part type: expected unsigned',
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, key)
        t.assert_error_covers(err, i.quantile, i, 0.5, nil, key)

        key = {1, 2, 3}
        err = {
            type = 'ClientError',
            name = 'KEY_PART_COUNT',
            message = 'Invalid key part count (expected [0..2], got 3)',
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, key)
        t.assert_error_covers(err, i.quantile, i, 0.5, nil, key)

        err = {
            type = 'IllegalParams',
            message = 'begin_key must be < end_key',
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, {10}, {5})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10}, {10})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10, 10}, {10})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10, 10}, {10, 5})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10, 10}, {10, 10})

        err = {
            type = 'ClientError',
            name = 'UNSUPPORTED_INDEX_FEATURE',
            message = "Index 'hash' (HASH) of space 'test' (memtx) does not " ..
                      "support quantile()",
        }
        local h = rv.space.test.index.hash
        t.assert_error_covers(err, h.quantile, h, 0.5, {10, 10}, {10, 10})

        rv:close()
    end)
end

g.after_test('test_quantile_invalid', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_quantile_after_close = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        local rv = box.read_view.open()
        local idx = rv.space.test.index.primary
        rv:close()
        local err = {type = 'ClientError', name = 'READ_VIEW_CLOSED'}
        t.assert_error_covers(err, idx.quantile, idx, 0.5, {}, {})
    end)
end

g.after_test('test_quantile_after_close', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_quantile_tree = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = 'tree'})
        box.space.test:create_index('secondary', {
            type = 'tree', parts = {2, 'string', 3, 'unsigned'},
        })
        box.space.test:insert{1, 'foo', 10}
        box.space.test:insert{2, 'bar', 20}
        box.space.test:insert{3, 'baz', 30}
        local rv = box.read_view.open()
        local s = rv.space.test
        box.space.test:replace{2, 'BAR', 200}
        box.space.test:delete({3})
        t.assert_equals(s.index.primary:quantile(0.25), {1})
        t.assert_equals(s.index.primary:quantile(0.5), {2})
        t.assert_equals(s.index.primary:quantile(0.75), {3})
        t.assert_equals(s.index.primary:quantile(0.25, {4}), nil)
        t.assert_equals(s.index.primary:quantile(0.25, {2}), {2})
        t.assert_equals(s.index.primary:quantile(0.75, {3}), {3})
        t.assert_equals(s.index.secondary:quantile(0.5), {'baz', 30})
        t.assert_equals(s.index.secondary:quantile(0.75,
            {'bar', 20}, {'foo', 11}), {'foo', 10})
        t.assert_equals(s.index.secondary:quantile(0.75,
            {'bar'}, {'foo'}), {'baz', 30})
        t.assert_equals(s.index.secondary:quantile(0.75,
            box.tuple.new{'bar', 20}, box.tuple.new{'foo', 10}), {'baz', 30})
        rv:close()
    end)
end

g.after_test('test_quantile_tree', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_quantile_multikey = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('multikey', {
            unique = false,
            parts = {{'[2][*]', 'string'}},
        })

        -- 1a 3b 5c 4d 2e
        box.space.test:insert({1, {'a', 'b', 'c'}})
        box.space.test:insert({2, {'b', 'b', 'c', 'c', 'd', 'd'}})
        box.space.test:insert({3, {'c', 'd', 'e'}})
        box.space.test:insert({4, {'c', 'd', 'e'}})

        local rv = box.read_view.open()
        local i = rv.space.test.index.multikey

        t.assert_equals(i:quantile(0.01), {'a'})
        t.assert_equals(i:quantile(0.10), {'b'})
        t.assert_equals(i:quantile(0.50), {'c'})
        t.assert_equals(i:quantile(0.80), {'d'})
        t.assert_equals(i:quantile(0.90), {'e'})

        t.assert_equals(i:quantile(0.5, {}, {'c'}), {'b'})
        t.assert_equals(i:quantile(0.5, {'c'}, {}), {'d'})
        t.assert_equals(i:quantile(0.5, {'b'}, {'e'}), {'c'})

        t.assert_equals(i:quantile(0.5, {}, {'a'}), nil)
        t.assert_equals(i:quantile(0.5, {'ff'}, {}), nil)
        t.assert_equals(i:quantile(0.5, {'aa'}, {'ab'}), nil)

        rv:close()
    end)
end

g.after_test('test_quantile_multikey', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_quantile_func = function(cg)
    cg.server:exec(function()
        box.schema.func.create('test', {
            is_deterministic = true,
            is_multikey = true,
            is_sandboxed = true,
            body = [[function(tuple)
                return {{tuple[2]}, {tuple[3]}}
            end]]
        })
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('func', {
            unique = false,
            func = 'test',
            parts = {{1, 'string'}}
        })

        -- 2a 2b 2c 2d 2e
        box.space.test:insert({1, 'a', 'b'})
        box.space.test:insert({2, 'b', 'c'})
        box.space.test:insert({3, 'c', 'd'})
        box.space.test:insert({4, 'd', 'e'})
        box.space.test:insert({5, 'e', 'a'})

        local rv = box.read_view.open()
        local i = rv.space.test.index.func

        t.assert_equals(i:quantile(0.1), {'a'})
        t.assert_equals(i:quantile(0.3), {'b'})
        t.assert_equals(i:quantile(0.5), {'c'})
        t.assert_equals(i:quantile(0.7), {'d'})
        t.assert_equals(i:quantile(0.9), {'e'})

        t.assert_equals(i:quantile(0.5, {}, {'d'}), {'b'})
        t.assert_equals(i:quantile(0.5, {'c'}, {}), {'d'})
        t.assert_equals(i:quantile(0.5, {'b'}, {'e'}), {'c'})

        t.assert_equals(i:quantile(0.5, {}, {'a'}), nil)
        t.assert_equals(i:quantile(0.5, {'ff'}, {}), nil)
        t.assert_equals(i:quantile(0.5, {'aa'}, {'ab'}), nil)

        rv:close()
    end)
end

g.after_test('test_quantile_func', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        if box.func.test ~= nil then
            box.func.test:drop()
        end
    end)
end)

-- Checks space:foo(...) -> space.index[0]:foo(...) shortcuts.
g.test_shortcuts = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:create_index('secondary', {parts = {2, 'unsigned'}})
        for i = 1, 3 do
            box.space.test:insert({i, i * 10})
        end
        box.schema.space.create('test_no_index')
        local rv = box.read_view.open()
        local s = rv.space.test_no_index
        t.assert_error_msg_equals(
            "No index #0 is defined in space 'test_no_index'",
            s.get, s, 1)
        t.assert_error_msg_equals(
            "No index #0 is defined in space 'test_no_index'",
            s.select, s)
        t.assert_error_msg_equals(
            "No index #0 is defined in space 'test_no_index'",
            s.pairs, s)
        t.assert_error_msg_equals(
            "No index #0 is defined in space 'test_no_index'",
            s.quantile, s, 0.5)
        s = rv.space.test
        t.assert_equals(s:get(1), {1, 10})
        t.assert_equals(s:get(10), nil)
        t.assert_equals(s:quantile(0.5), {2})
        t.assert_equals(s:quantile(0.5, {10}), nil)
        t.assert_equals(s:quantile(0.9, {}, {3}), {2})
        t.assert_equals(s:select(), {{1, 10}, {2, 20}, {3, 30}})
        t.assert_equals(s:select({1}), {{1, 10}})
        t.assert_equals(s:select({10}), {})
        t.assert_equals(s:select({1}, 'gt'), {{2, 20}, {3, 30}})
        t.assert_equals(s:select({1}, {iterator = 'ge', offset = 1, limit = 1}),
                        {{2, 20}})
        local function do_pairs(...)
            local result = {}
            for k, v in s:pairs(...) do
                result[k] = v
            end
            return result
        end
        t.assert_equals(do_pairs(), {{1, 10}, {2, 20}, {3, 30}})
        t.assert_equals(do_pairs(2), {{2, 20}})
        t.assert_equals(do_pairs({3}, 'lt'), {{2, 20}, {1, 10}})
        rv:close()
    end)
end

g.after_test('test_shortcuts', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        if box.space.test_no_index then
            box.space.test_no_index:drop()
        end
    end)
end)

-- Checks that tuple fields retrieved from a read view can be accessed by name.
g.test_field_names = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:format({
            {'foo', 'unsigned'},
            {'bar', 'string'},
            {'baz', 'unsigned'},
        })
        box.space.test:create_index('primary')
        box.space.test:insert({1, 'a', 10})
        box.space.test:insert({2, 'b', 20})
        box.space.test:insert({3, 'c', 30})
        local rv = box.read_view.open()
        local s = rv.space.test
        box.space.test:format({
            {'x', 'unsigned'},
            {'y', 'string'},
        })
        box.space.test:replace({1, 'aa', 100})
        t.assert_covers(s:get(1):tomap(),
                        {foo = 1, bar = 'a', baz = 10})
        t.assert_covers(s:select({}, 'le')[1]:tomap(),
                        {foo = 3, bar = 'c', baz = 30})
        rv:close()
    end)
end

g.after_test('test_field_names', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that spaces that are not accessible by the current user are excluded
-- from a read view.
g.test_access = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        box.schema.space.create('test_not_accessible')
        box.space.test_not_accessible:create_index('primary')
        box.space.test_not_accessible:insert({10})
        box.schema.user.create('alice')
        box.schema.user.grant('alice', 'read', 'space', 'test')
        box.session.su('alice', function()
            local rv = box.read_view.open()
            t.assert_is_not(rv.space.test, nil)
            t.assert_equals(rv.space.test:select(), {{1}})
            t.assert_is(rv.space.test_not_accessible, nil)
            rv:close()
        end)
    end)
end

g.after_test('test_access', function(cg)
    cg.server:exec(function()
        box.schema.user.drop('alice')
        if box.space.test then
            box.space.test:drop()
        end
        if box.space.test_not_accessible then
            box.space.test_not_accessible:drop()
        end
    end)
end)

g.before_test('test_limit_iteration', function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('p')
        for i = 1, 1000 do
            s:insert{i}
        end
    end)
end)

-- Checks that iteration over read views is limited with timeout. Basically,
-- luacheck: no max comment line length
-- copy-paste of https://github.com/tarantool/tarantool/blob/cba4e20aa2600713b55bdaad6fd84396257835f2/test/box-luatest/gh_6085_limit_iteration_in_space_test.lua#L32-L47
g.test_limit_iteration = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local check_fiber_slice = function(fiber_f)
            local fib = fiber.new(fiber_f)
            fib:set_joinable(true)
            local _, err = fib:join()
            t.assert_equals(tostring(err), "fiber slice is exceeded")
        end

        local rv = box.read_view.open()

        local function endless_select_func()
            fiber.set_slice(0.2)
            while true do
                rv.space.s:select{}
            end
        end
        check_fiber_slice(endless_select_func)

        local function endless_get_func()
            fiber.set_slice(0.2)
            while true do
                for i = 1, 1000 do
                    rv.space.s:get{i}
                end
            end
        end
        check_fiber_slice(endless_get_func)

        local function endless_pairs_func()
            fiber.set_slice(0.2)
            while true do
                for _, _ in rv.space.s:pairs() do end
            end
        end
        check_fiber_slice(endless_pairs_func)
    end)
end

g.after_test('test_limit_iteration', function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

-- Checks the read view sort order implementation (comparators to be precise).
g.test_read_view_sort_order = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.space.test:create_index('asc', {
            parts = {{2, 'unsigned', is_nullable = true, sort_order = 'asc'}},
            hint = false,
            unique = false,
        })
        box.space.test:create_index('desc', {
            parts = {{2, 'unsigned', is_nullable = true, sort_order = 'desc'}},
            hint = false,
            unique = false,
        })
        box.space.test:insert({0, box.NULL})
        for i = 1, 10 do
            box.space.test:insert({i, math.floor(i / 2)})
        end
        local rv = box.read_view.open()
        local asc = box.space.test.index.asc
        local desc = box.space.test.index.desc
        local rv_asc = rv.space.test.index.asc
        local rv_desc = rv.space.test.index.desc
        for it in pairs({'LT', 'LE', 'GE', 'GT', 'EQ', 'REQ'}) do
            for key = 0, 5 do
                t.assert_equals(asc:select(key, {iterator = it}),
                                rv_asc:select(key, {iterator = it}))
                t.assert_equals(desc:select(key, {iterator = it}),
                                rv_desc:select(key, {iterator = it}))
            end
            t.assert_equals(asc:select(box.NULL, {iterator = it}),
                            rv_asc:select(box.NULL, {iterator = it}))
            t.assert_equals(desc:select(box.NULL, {iterator = it}),
                            rv_desc:select(box.NULL, {iterator = it}))
        end
        rv:close()
    end)
end

g.after_test('test_read_view_sort_order', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks the read view sort order implementation regarding functional indexes.
g.test_read_view_sort_order_func_indexes = function(cg)
    local function test(pk_sort_order, fk_sort_order)
        local space_name = 's_' .. pk_sort_order .. '_' .. fk_sort_order
        local func_name = 'f_' .. pk_sort_order .. '_' .. fk_sort_order
        box.schema.func.create(func_name, {
            body = [[function(t) return {t[2]} end]],
            is_deterministic = true,
            is_sandboxed = true,
        })
        local s = box.schema.space.create(space_name)
        s:create_index('pk', {
            parts = {{1, 'unsigned', sort_order = pk_sort_order}},
        })
        s:create_index('fk', {
            parts = {
                {1, 'unsigned', sort_order = fk_sort_order, is_nullable = true}
            },
            func = box.func[func_name].id,
            hint = false,
            unique = false,
        })
        s:insert({0, box.NULL})
        for i = 1, 10 do
            s:insert({i, math.floor(i / 2)})
        end
        local rv = box.read_view.open()
        local fk = s.index.fk
        local rv_fk = rv.space[s.name].index.fk
        for it in pairs({'LT', 'LE', 'GE', 'GT', 'EQ', 'REQ'}) do
            for key = 0, 5 do
                t.assert_equals(fk:select(key, {iterator = it}),
                                rv_fk:select(key, {iterator = it}))
            end
            t.assert_equals(fk:select(box.NULL, {iterator = it}),
                            rv_fk:select(box.NULL, {iterator = it}))
        end
        rv:close()
    end

    cg.server:exec(test, {'asc', 'asc'})
    cg.server:exec(test, {'asc', 'desc'})
    cg.server:exec(test, {'desc', 'asc'})
    cg.server:exec(test, {'desc', 'desc'})
end

g.after_test('test_read_view_sort_order_func_indexes', function(cg)
    cg.server:exec(function()
        local test_suffixes = { 'asc_asc', 'asc_desc', 'desc_asc', 'desc_desc' }
        for suffix in pairs(test_suffixes) do
            if box.space['s_' .. suffix] then
                box.space['s_' .. suffix]:drop()
            end
            box.schema.func.drop('f_' .. suffix, {if_exists = true})
        end
    end)
end)

g.test_read_view_get_after_space_drop = function(cg)
    cg.server:exec(function()
        local test = box.schema.space.create('test')
        local pk = test:create_index('primary')
        local rv = box.read_view.open()
        test:drop()
        t.assert_error_covers({
            space = 'test',
            index = 'primary',
            space_id = test.id,
            index_id = pk.id,
            key = {},
            message = 'Invalid key part count in an exact match ' ..
                      '(expected 1, got 0)'
        }, rv.space.test.get, rv.space.test, {})
        rv:close()
    end)
end

local g_mvcc = t.group('read_view.mvcc', t.helpers.matrix{
    func_index = {true, false},
})

g_mvcc.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g_mvcc.after_all(function(cg)
    cg.server:drop()
end)

g_mvcc.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        if box.func.test ~= nil then
            box.func.test:drop()
        end
    end)
end)

g_mvcc.test_secondary_index_clarification = function(cg)
    cg.server:exec(function(func_index)
        local s = box.schema.space.create('test')
        s:create_index('pk')
        if func_index then
            box.schema.func.create('test', {
                is_deterministic = true,
                is_sandboxed = true,
                body = [[function(tuple)
                    return {tuple[2]}
                end]]
            })
            s:create_index('sk', {parts = {{1, 'unsigned'}}, func = 'test'})
        else
            s:create_index('sk', {parts = {{2, 'unsigned'}}})
        end

        s:replace{1, 1}
        s:replace{2, 2}

        box.begin()
        s:replace{1, 10}
        s:delete{2}
        local rv = box.read_view.open()
        box.commit()

        t.assert_equals(rv.space.test:get(1), {1, 1})
        t.assert_equals(rv.space.test:get(2), {2, 2})
        t.assert_equals(rv.space.test.index.sk:get(1), {1, 1})
        t.assert_equals(rv.space.test.index.sk:get(2), {2, 2})
        t.assert_equals(rv.space.test.index.sk:get(10), nil)
        rv:close()
    end, {cg.params.func_index})
end

g_mvcc.test_select_offset_cleaner = function(cg)
    cg.server:exec(function(func_index)
        -- The test space.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        if func_index then
            box.schema.func.create('test', {
                is_deterministic = true,
                is_sandboxed = true,
                body = [[function(tuple)
                    return {tuple[2]}
                end]]
            })
            s:create_index('sk', {parts = {1, 'unsigned',
                                           is_nullable = true,
                                           exclude_null = true},
                                  func = 'test'})
        else
            s:create_index('sk', {parts = {2, 'unsigned',
                                           is_nullable = true,
                                           exclude_null = true}})
        end
        local sk = s.index.sk

        -- Fill the space.
        s:insert({3, 3})
        s:insert({5, 5})
        s:insert({7, 7})
        s:insert({9, 9})
        s:insert({11, 11})
        s:insert({13, 13})

        -- The function to check selection results.
        local function check(i)
            -- Invisible count.
            t.assert_equals(i:select(nil, {offset = 4}), {{11, 11}, {13, 13}})
            t.assert_equals(i:select({12}, {iterator = 'lt', offset = 3}),
                            {{5, 5}, {3, 3}})
            t.assert_equals(i:select({12}, {iterator = 'le', offset = 3}),
                            {{5, 5}, {3, 3}})
            t.assert_equals(i:select({2}, {iterator = 'ge', offset = 3}),
                            {{9, 9}, {11, 11}, {13, 13}})
            t.assert_equals(i:select({2}, {iterator = 'gt', offset = 3}),
                            {{9, 9}, {11, 11}, {13, 13}})

            -- Too big offset.
            t.assert_equals(i:select({4}, {iterator = 'lt', offset = 3}), {})
            t.assert_equals(i:select({3}, {iterator = 'le', offset = 3}), {})
            t.assert_equals(i:select({7}, {iterator = 'eq', offset = 1}), {})
            t.assert_equals(i:select({7}, {iterator = 'eq', offset = 3}), {})
            t.assert_equals(i:select({7}, {iterator = 'req', offset = 1}), {})
            t.assert_equals(i:select({7}, {iterator = 'req', offset = 3}), {})
            t.assert_equals(i:select({13}, {iterator = 'ge', offset = 3}), {})
            t.assert_equals(i:select({12}, {iterator = 'gt', offset = 3}), {})
        end

        -- Check the expected results.
        check(sk)

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local other = txn_proxy.new()

        -- Insert invisible tuples.
        other:begin()
        other("box.space.test:insert({0, 0})")
        other("box.space.test:insert({1, box.NULL})")
        other("box.space.test:insert({2, 2})")
        other("box.space.test:delete({3})")
        other("box.space.test:insert({4, 4})")
        other("box.space.test:replace({5, 10})")
        other("box.space.test:insert({6, 6})")
        other("box.space.test:replace({7, box.NULL})")
        other("box.space.test:insert({8, 80})")
        other("box.space.test:delete({9})")
        other("box.space.test:insert({10, 100})")
        other("box.space.test:replace({11, box.NULL})")
        other("box.space.test:insert({12, 12})")
        other("box.space.test:delete({13})")
        other("box.space.test:insert({14, box.NULL})")
        other("box.space.test:insert({15, 15})")

        -- The result is still the same.
        check(sk)

        -- Create a read view now.
        local rv = box.read_view.open()

        -- Check the read view select result before the commit.
        check(rv.space.test.index.sk)

        -- Check the read view select result after the commit.
        other:commit()
        check(rv.space.test.index.sk)

        -- Clean-up.
        rv:close()
    end, {cg.params.func_index})
end

g_mvcc.test_count_cleaner = function(cg)
    cg.server:exec(function(func_index)
        -- The test space.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        if func_index then
            box.schema.func.create('test', {
                is_deterministic = true,
                is_sandboxed = true,
                body = [[function(tuple)
                    return {tuple[2]}
                end]]
            })
            s:create_index('sk', {parts = {1, 'unsigned',
                                           is_nullable = true,
                                           exclude_null = true},
                                  unique = false,
                                  func = 'test'})
        else
            s:create_index('sk', {parts = {2, 'unsigned',
                                           is_nullable = true,
                                           exclude_null = true},
                                  unique = false})
        end
        local sk = s.index.sk

        -- Fill the space.
        s:insert({3, 3})
        s:insert({5, box.NULL})
        s:insert({7, 7})
        s:insert({9, box.NULL})
        s:insert({11, 11})
        s:insert({13, box.NULL})
        s:insert({15, 15})
        s:insert({17, 11})

        -- The function to check selection results.
        local function check(i)
            -- Count all.
            t.assert_equals(i:count(nil), 5)
            t.assert_equals(i:count({17}, {iterator = 'lt'}), 5)
            t.assert_equals(i:count({17}, {iterator = 'le'}), 5)
            t.assert_equals(i:count({0}, {iterator = 'ge'}), 5)
            t.assert_equals(i:count({0}, {iterator = 'gt'}), 5)

            -- Count some.
            t.assert_equals(i:count({13}, {iterator = 'lt'}), 4)
            t.assert_equals(i:count({12}, {iterator = 'le'}), 4)
            t.assert_equals(i:count({4}, {iterator = 'ge'}), 4)
            t.assert_equals(i:count({3}, {iterator = 'gt'}), 4)

            -- eq/req.
            t.assert_equals(i:count({1}, {iterator = 'eq'}), 0)
            t.assert_equals(i:count({1}, {iterator = 'req'}), 0)
            t.assert_equals(i:count({10}, {iterator = 'eq'}), 0)
            t.assert_equals(i:count({10}, {iterator = 'req'}), 0)
            t.assert_equals(i:count({17}, {iterator = 'eq'}), 0)
            t.assert_equals(i:count({17}, {iterator = 'req'}), 0)
            t.assert_equals(i:count({7}, {iterator = 'eq'}), 1)
            t.assert_equals(i:count({7}, {iterator = 'req'}), 1)
            t.assert_equals(i:count({11}, {iterator = 'eq'}), 2)
            t.assert_equals(i:count({11}, {iterator = 'req'}), 2)
        end

        -- Check the expected results.
        check(sk)

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local other = txn_proxy.new()

        -- Insert something in parallel.
        other:begin()
        other("box.space.test:insert({0, 0})")
        other("box.space.test:insert({1, box.NULL})")
        other("box.space.test:insert({2, 2})")
        other("box.space.test:delete({3})")
        other("box.space.test:insert({4, 4})")
        other("box.space.test:replace({5, 10})")
        other("box.space.test:insert({6, 6})")
        other("box.space.test:replace({7, box.NULL})")
        other("box.space.test:insert({8, 80})")
        other("box.space.test:delete({9})")
        other("box.space.test:insert({10, 100})")
        other("box.space.test:replace({11, box.NULL})")
        other("box.space.test:insert({12, 12})")
        other("box.space.test:delete({13})")
        other("box.space.test:insert({14, box.NULL})")
        other("box.space.test:insert({16, 16})")
        other("box.space.test:replace({17, 10})")
        other("box.space.test:insert({18, 11})")

        -- The result is still the same.
        check(sk)

        -- Create a read view now.
        local rv = box.read_view.open()

        -- Check the read view count result before the commit.
        check(rv.space.test.index.sk)

        -- Check the read view count result after the commit.
        other:commit()
        check(rv.space.test.index.sk)

        -- Clean-up.
        rv:close()
    end, {cg.params.func_index})
end
