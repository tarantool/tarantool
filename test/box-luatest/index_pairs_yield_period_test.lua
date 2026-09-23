local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    -- Define helpers in the server process; globals persist across exec calls.
    cg.server:exec(function()
        -- Fill a space with 1000 two-field tuples (field 2 lets us build
        -- secondary indexes).
        rawset(_G, 'fill', function(s)
            box.begin()
            for i = 1, 1000 do
                s:replace({i, i})
            end
            box.commit()
        end)
        -- Scan `iter` (a space or an index) and report (count, did_yield).
        --
        -- did_yield uses a freshly created fiber: thanks to cooperative
        -- scheduling it is ready but only actually runs if the scanning fiber
        -- yields somewhere inside the loop. So `ran == true` after the loop is
        -- a deterministic "the scan yielded at least once" detector.
        rawset(_G, 'scan', function(iter, opts)
            local fiber = require('fiber')
            local ran = false
            fiber.new(function() ran = true end)
            local n = 0
            for _ in iter:pairs({}, opts) do
                n = n + 1
            end
            return n, ran
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- memtx TREE primary index (baseline).
g.test_memtx_tree_primary = function(cg)
    local n, ran, n2, ran2 = cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        fill(s)
        local a, b = scan(s, {yield_period = 100})
        local c, d = scan(s, {})
        return a, b, c, d
    end)
    t.assert_equals(n, 1000)
    t.assert(ran, 'tree scan with yield_period must yield')
    t.assert_equals(n2, 1000)
    t.assert_not(ran2, 'default tree scan must not yield')
end

-- memtx TREE secondary index.
g.test_memtx_tree_secondary = function(cg)
    local n, ran, n2, ran2 = cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {parts = {2, 'unsigned'}})
        fill(s)
        local a, b = scan(s.index.sk, {yield_period = 100})
        local c, d = scan(s.index.sk, {})
        return a, b, c, d
    end)
    t.assert_equals(n, 1000)
    t.assert(ran, 'secondary tree scan with yield_period must yield')
    t.assert_equals(n2, 1000)
    t.assert_not(ran2, 'default secondary tree scan must not yield')
end

-- memtx HASH index.
g.test_memtx_hash = function(cg)
    local n, ran, n2, ran2 = cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {type = 'HASH'})
        fill(s)
        local a, b = scan(s, {yield_period = 100})
        local c, d = scan(s, {})
        return a, b, c, d
    end)
    t.assert_equals(n, 1000)
    t.assert(ran, 'hash scan with yield_period must yield')
    t.assert_equals(n2, 1000)
    t.assert_not(ran2, 'default hash scan must not yield')
end

-- memtx BITSET secondary index (non-unique, ITER_ALL).
g.test_memtx_bitset = function(cg)
    local n, ran, n2, ran2 = cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('bs', {type = 'BITSET', unique = false,
                              parts = {2, 'unsigned'}})
        fill(s)
        local a, b = scan(s.index.bs, {yield_period = 100})
        local c, d = scan(s.index.bs, {})
        return a, b, c, d
    end)
    t.assert_equals(n, 1000)
    t.assert(ran, 'bitset scan with yield_period must yield')
    t.assert_equals(n2, 1000)
    t.assert_not(ran2, 'default bitset scan must not yield')
end

-- Option validation (engine-agnostic).
g.test_validation = function(cg)
    cg.server:exec(function()
        local tt = require('luatest')
        local s = box.schema.space.create('test')
        s:create_index('pk')
        tt.assert_error_msg_contains(
            'yield_period must be a non-negative integer',
            function() s:pairs({}, {yield_period = -1}) end)
        tt.assert_error_msg_contains(
            'yield_period must be a non-negative integer',
            function() s:pairs({}, {yield_period = 1.5}) end)
        tt.assert_error_msg_contains(
            'yield_period must be a non-negative integer',
            function() s:pairs({}, {yield_period = 'x'}) end)
        -- zero is allowed and means "never yield".
        local n = 0
        for _ in s:pairs({}, {yield_period = 0}) do n = n + 1 end
        tt.assert_equals(n, 0)
    end)
end
