local t = require('luatest')

local g = t.group('gh-6119-select-big-numbers', {{engine = 'memtx'},
                                                 {engine = 'vinyl'}})

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.before_test('test_select_big_number', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('primary', {parts = {{1, 'number'}}})
    end, {cg.params.engine})
end)

g.after_test('test_select_big_number', function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Test from #6119, select by big number is space with big numbers.
g.test_select_big_number = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        local uint_max = -1ULL
        local big_num = 1e100
        s:insert({uint_max})
        s:insert({big_num})
        t.assert_equals(s:select({1e50}, {iterator = 'GE'}), {{big_num}})
        t.assert_equals(s:select({1e50}, {iterator = 'LE'}), {{uint_max}})
    end, {})
end
