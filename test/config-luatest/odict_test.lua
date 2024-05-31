local fun = require('fun')
local ffi = require('ffi')
local odict = require('internal.config.utils.odict')
local t = require('luatest')

local g = t.group()

g.test_set_get = function()
    local od = odict.new()

    od.a = 1
    od.b = 2
    od.c = 3

    t.assert_equals(od.a, 1)
    t.assert_equals(od.b, 2)
    t.assert_equals(od.c, 3)
end

g.test_delete_non_existing = function()
    local od = odict.new()

    od.a = nil

    t.assert_equals(od.a, nil)
end

g.test_delete = function()
    local od = odict.new()

    od.a = 1
    od.b = 2
    od.c = 3

    od.b = nil

    t.assert_equals(od.a, 1)
    t.assert_equals(od.b, nil)
    t.assert_equals(od.c, 3)
end

g.test_pairs = function()
    local od = odict.new()

    od.a = 1
    od.b = 2
    od.c = 3

    local res = {}
    for k, v in odict.pairs(od) do
        table.insert(res, {k, v})
    end
    t.assert_equals(res, {
        {'a', 1},
        {'b', 2},
        {'c', 3},
    })
end

g.test_pairs_set = function()
    local od = odict.new()

    od.a = 1
    od.b = 2
    od.c = 3

    od.b = 2.1

    local res = {}
    for k, v in odict.pairs(od) do
        table.insert(res, {k, v})
    end
    t.assert_equals(res, {
        {'a', 1},
        {'b', 2.1},
        {'c', 3},
    })
end


g.test_pairs_delete = function()
    local od = odict.new()

    od.a = 1
    od.b = 2
    od.c = 3

    od.b = nil

    local res = {}
    for k, v in odict.pairs(od) do
        table.insert(res, {k, v})
    end
    t.assert_equals(res, {
        {'a', 1},
        {'c', 3},
    })
end

g.test_pairs_delete_and_set = function()
    local od = odict.new()

    od.a = 1
    od.b = 2
    od.c = 3

    od.b = nil
    od.b = 4

    local res = {}
    for k, v in odict.pairs(od) do
        table.insert(res, {k, v})
    end
    t.assert_equals(res, {
        {'a', 1},
        {'c', 3},
        {'b', 4},
    })
end

-- Using cdata as a key is unusual, but it is legal in LuaJIT.
--
-- There is a potential pitfall: ffi.new('void *') == nil. Let's
-- verify that we handle such a key correctly.
g.test_null_as_key = function()
    local od = odict.new()

    local nulls = {
        ffi.new('void *'),
        ffi.new('void *'),
        ffi.new('void *'),
    }

    od[nulls[1]] = 1
    od[nulls[2]] = 2
    od[nulls[3]] = 3

    local res = {}
    for k, v in odict.pairs(od) do
        table.insert(res, {k, v})
    end

    -- It doesn't differentiate nulls.
    t.assert_equals(res, {
        {nulls[1], 1},
        {nulls[2], 2},
        {nulls[3], 3},
    })

    -- It does.
    t.assert(rawequal(res[1][1], nulls[1]))
    t.assert(rawequal(res[2][1], nulls[2]))
    t.assert(rawequal(res[3][1], nulls[3]))
end

-- {{{ Helpers for reindexing test cases

-- Parse a range expressed like Python's slice operator.
--
-- '1:3000' -> 1, 3000
-- '1:3000:2' -> 1, 3000, 2
--
-- The result is suitable to pass into fun.range().
local function parse_range(s)
    local start, stop, step = unpack(s:split(':', 2))
    start = tonumber(start)
    stop = tonumber(stop)
    step = tonumber(step)
    return start, stop, step
end

-- Generate a value based on the given prefix and the given key.
local function gen_value(prefix, key)
    return ('%s-%s'):format(prefix, key)
end

local slice_mt = {
    __newindex = function(self, k, v)
        fun.range(parse_range(k)):each(function(k)
            if type(v) == 'nil' then
                self.od[k] = nil
            else
                self.od[k] = gen_value(v, k)
            end
        end)
    end,
}

-- Bulk assign values to a table using a slicing operator, similar
-- to Python's one.
local function slice(od)
    return setmetatable({od = od}, slice_mt)
end

-- Verify that the ordered dictionary returns given values in the
-- given order.
--
-- The values are described in the following way.
--
-- {
--     '<range> <prefix>',
--     ...
-- }
--
-- <range> is a start:stop or a start:stop:step string, see
-- parse_range().
--
-- <prefix> is a value prefix, see gen_value().
local function check(od, values)
    local res = {}
    for k, v in odict.pairs(od) do
        table.insert(res, {k, v})
    end

    local exp = {}
    for _, v in ipairs(values) do
        local range_str, prefix = unpack(v:split(' ', 1))
        fun.range(parse_range(range_str)):each(function(k)
            table.insert(exp, {k, gen_value(prefix, k)})
        end)
    end

    t.assert_equals(res, exp)
end

-- Access the internal registry of ordered dictionaries.
local function registry()
    for i = 1, debug.getinfo(odict.new).nups do
        local name, var = debug.getupvalue(odict.new, i)
        if name == 'registry' then
            return var
        end
    end
    assert(false)
end

-- Look on the maximum ID in the internal key<->id mappings.
--
-- Useful to verify that the reindexing actually occurs.
local function max_id(od)
    return registry()[od].max_id
end

-- }}} Helpers for reindexing test cases

-- {{{ Reindexing test cases

-- Fill 6000 values, clear first 3000, fill 3000 more. Verify that
-- the order is correct for all the remaining values.
--
-- Sketchy illustration of these assignments is below.
--
--         1 .. 3000 .. 6000 .. 9000
-- step 1   aaaa
-- step 2   aaaa    bbbb
-- step 3           bbbb
-- step 4           bbbb    cccc
--
-- The expected order is bbbb cccc.
g.test_reindex_delete_head = function()
    local od = odict.new()

    slice(od)['1:3000'] = 'aaaa'
    slice(od)['3001:6000'] = 'bbbb'
    slice(od)['1:3000'] = nil
    slice(od)['6001:9000'] = 'cccc'

    check(od, {
        '3001:6000 bbbb',
        '6001:9000 cccc',
    })
    t.assert_equals(max_id(od), 6000)
end

-- Fill 6000 values, clear first 3000, fill 3000 more, reassign
-- first 3000 ones. Verify that the order is correct for all
-- the remaining values.
--
-- Sketchy illustration of these assignments is below.
--
--         1 .. 3000 .. 6000 .. 9000
-- step 1   aaaa
-- step 2   aaaa    bbbb
-- step 3           bbbb
-- step 4           bbbb    cccc
-- step 5   dddd    bbbb    cccc
--
-- The expected order is bbbb cccc dddd.
g.test_reindex_reassing_head = function()
    local od = odict.new()

    slice(od)['1:3000'] = 'aaaa'
    slice(od)['3001:6000'] = 'bbbb'
    slice(od)['1:3000'] = nil
    slice(od)['6001:9000'] = 'cccc'
    slice(od)['1:3000'] = 'dddd'

    check(od, {
        '3001:6000 bbbb',
        '6001:9000 cccc',
        '1:3000 dddd',
    })
    t.assert_equals(max_id(od), 9000)
end

-- Fill 6000 values, clear last 3000 ones, fill 3000 more. Verify
-- that the order is correct for all the remaining values.
--
-- Sketchy illustration of these assignments is below.
--
--         1 .. 3000 .. 6000 .. 9000
-- step 1   aaaa
-- step 2   aaaa    bbbb
-- step 3   aaaa
-- step 4   aaaa            cccc
--
-- The expected order is aaaa cccc.
g.test_reindex_delete_middle = function()
    local od = odict.new()

    slice(od)['1:3000'] = 'aaaa'
    slice(od)['3001:6000'] = 'bbbb'
    slice(od)['3001:6000'] = nil
    slice(od)['6001:9000'] = 'cccc'

    check(od, {
        '1:3000 aaaa',
        '6001:9000 cccc',
    })
    t.assert_equals(max_id(od), 6000)
end

-- Fill 6000 values, clear last 3000 ones, fill 3000 more,
-- reassing the cleared values. Verify that the order is
-- correct for all the remaining values.
--
-- Sketchy illustration of these assignments is below.
--
--         1 .. 3000 .. 6000 .. 9000
-- step 1   aaaa
-- step 2   aaaa    bbbb
-- step 3   aaaa
-- step 4   aaaa            cccc
-- step 5   aaaa    dddd    cccc
--
-- The expected order is aaaa cccc dddd.
g.test_reindex_reassign_middle = function()
    local od = odict.new()

    slice(od)['1:3000'] = 'aaaa'
    slice(od)['3001:6000'] = 'bbbb'
    slice(od)['3001:6000'] = nil
    slice(od)['6001:9000'] = 'cccc'
    slice(od)['3001:6000'] = 'dddd'

    check(od, {
        '1:3000 aaaa',
        '6001:9000 cccc',
        '3001:6000 dddd',
    })
    t.assert_equals(max_id(od), 9000)
end

-- Fill 9000 values, clear the last 3000 ones. Verify that the
-- order is correct for all the remaining values.
--
-- Sketchy illustration of these assignments is below.
--
--         1 .. 3000 .. 6000 .. 9000
-- step 1   aaaa
-- step 2   aaaa    bbbb
-- step 3           bbbb    cccc
-- step 4           bbbb
--
-- The expected order is aaaa bbbb.
g.test_reindex_delete_tail = function()
    local od = odict.new()

    slice(od)['1:3000'] = 'aaaa'
    slice(od)['3001:6000'] = 'bbbb'
    slice(od)['6001:9000'] = 'cccc'
    slice(od)['6001:9000'] = nil

    check(od, {
        '1:3000 aaaa',
        '3001:6000 bbbb',
    })

    -- This test case doesn't involve reindexing after deletion of
    -- the 6001:9000 fields. So, we check the order of remaining
    -- values and don't check the maximum ID in the internal
    -- key<->id mappings.
end

-- Fill 9000 values, clear the last 3000 ones, then reassign them.
-- Verify that the order is correct for all the remaining values.
--
-- Sketchy illustration of these assignments is below.
--
--         1 .. 3000 .. 6000 .. 9000
-- step 1   aaaa
-- step 2   aaaa    bbbb
-- step 3           bbbb    cccc
-- step 4           bbbb
-- step 5           bbbb    dddd
--
-- The expected order is aaaa bbbb dddd.
g.test_reindex_reassign_tail = function()
    local od = odict.new()

    slice(od)['1:3000'] = 'aaaa'
    slice(od)['3001:6000'] = 'bbbb'
    slice(od)['6001:9000'] = 'cccc'
    slice(od)['6001:9000'] = nil
    slice(od)['6001:9000'] = 'dddd'

    check(od, {
        '1:3000 aaaa',
        '3001:6000 bbbb',
        '6001:9000 dddd',
    })

    -- This test case doesn't involve reindexing after reassigning
    -- of the 6001:9000 fields. So, we check the order of
    -- remaining values and don't check the maximum ID in the
    -- internal key<->id mappings.
end

-- }}} Reindexing test cases
