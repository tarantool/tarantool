local server = require('luatest.server')
local t = require('luatest')

local g = t.group('vinyl.tx_stmt_overwrite', t.helpers.matrix{
    unique = {false, true},
    defer_deletes = {false, true},
})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(params)
        box.schema.space.create('test1', {
            engine = 'vinyl',
            defer_deletes = params.defer_deletes,
        })
        box.space.test1:create_index('i1')
        box.space.test1:create_index('i2', {
            unique = params.unique,
            parts = {{2, 'unsigned'}},
        })
        box.schema.space.create('test2', {
            engine = 'vinyl',
            defer_deletes = params.defer_deletes,
        })
        box.space.test2:create_index('i1')
        box.space.test2:create_index('i2', {
            unique = params.unique,
            parts = {{2, 'unsigned'}},
        })
        box.space.test2:create_index('i3', {
            unique = params.unique,
            parts = {{3, 'unsigned'}},
        })
    end, {cg.params})
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

g.test_case_1 = function(cg)
    cg.server:exec(function()
        local s = box.space.test1
        s:replace{1, 2}
        box.begin()
        s:replace{1, 1}
        s:replace{1, 1}
        box.commit()
        box.snapshot()
        local data = {{1, 1}}
        t.assert_equals(s.index.i1:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i2:select({}, {fullscan = true}), data)
        local stat = {memory = {rows = 0}, disk = {rows = 1}}
        t.assert_covers(s.index.i1:stat(), stat)
        t.assert_covers(s.index.i2:stat(), stat)
    end)
end

g.test_case_2 = function(cg)
    cg.server:exec(function()
        local s = box.space.test1
        s:replace{1, 1}
        box.begin()
        s:replace{1, 1}
        s:upsert({1, 2}, {{'=', 2, 2}})
        box.commit()
        box.snapshot()
        local data = {{1, 2}}
        t.assert_equals(s.index.i1:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i2:select({}, {fullscan = true}), data)
        local stat = {memory = {rows = 0}, disk = {rows = 1}}
        t.assert_covers(s.index.i1:stat(), stat)
        t.assert_covers(s.index.i2:stat(), stat)
    end)
end

g.test_case_3 = function(cg)
    cg.server:exec(function()
        local s = box.space.test1
        s:replace{1, 1}
        box.begin()
        s:update(1, {{'=', 2, 2}})
        s:update(1, {{'=', 2, 3}})
        box.commit()
        box.snapshot()
        local data = {{1, 3}}
        t.assert_equals(s.index.i1:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i2:select({}, {fullscan = true}), data)
        local stat = {memory = {rows = 0}, disk = {rows = 1}}
        t.assert_covers(s.index.i1:stat(), stat)
        t.assert_covers(s.index.i2:stat(), stat)
    end)
end

g.test_case_4 = function(cg)
    cg.server:exec(function()
        local s = box.space.test2
        s:replace{1, 2, 3}
        box.begin()
        s:upsert({1, 3, 3}, {{'=', 2, 3}, {'=', 3, 3}})
        s:delete({1})
        box.commit()
        box.snapshot()
        local data = {}
        t.assert_equals(s.index.i1:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i2:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i3:select({}, {fullscan = true}), data)
        local stat = {memory = {rows = 0}, disk = {rows = 0}}
        t.assert_covers(s.index.i1:stat(), stat)
        t.assert_covers(s.index.i2:stat(), stat)
        t.assert_covers(s.index.i3:stat(), stat)
    end)
end

g.test_case_5 = function(cg)
    cg.server:exec(function()
        local s = box.space.test2
        box.begin()
        s:upsert({1, 3, 3}, {{'=', 2, 3}, {'=', 3, 3}})
        s:delete({1})
        box.commit()
        box.snapshot()
        local data = {}
        t.assert_equals(s.index.i1:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i2:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i3:select({}, {fullscan = true}), data)
        local stat = {memory = {rows = 0}, disk = {rows = 0}}
        t.assert_covers(s.index.i1:stat(), stat)
        t.assert_covers(s.index.i2:stat(), stat)
        t.assert_covers(s.index.i3:stat(), stat)
    end)
end

g.test_case_6 = function(cg)
    cg.server:exec(function()
        local s = box.space.test2
        box.begin()
        s:upsert({1, 3, 3}, {{'=', 2, 3}, {'=', 3, 3}})
        s:replace({1, 2, 3})
        box.commit()
        box.snapshot()
        local data = {{1, 2, 3}}
        t.assert_equals(s.index.i1:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i2:select({}, {fullscan = true}), data)
        t.assert_equals(s.index.i3:select({}, {fullscan = true}), data)
        local stat = {memory = {rows = 0}, disk = {rows = 1}}
        t.assert_covers(s.index.i1:stat(), stat)
        t.assert_covers(s.index.i2:stat(), stat)
        t.assert_covers(s.index.i3:stat(), stat)
    end)
end
