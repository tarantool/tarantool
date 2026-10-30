local t = require('luatest')

local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix{engine = {'memtx', 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
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

g.test_tuple_lost_in_unique_nullable_index = function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {
            engine = engine,
            format = {
                {name = 'a', type = 'unsigned'},
                {name = 'b', type = 'unsigned', is_nullable = true},
            },
        })
        s:create_index('primary', {parts = {'a'}})
        s:create_index('secondary', {parts = {'b'}, unique = true})
        s:insert({1, 10})
        box.atomic(function()
            s:replace({1, 20})
            s:insert({2, 10})
        end)
        t.assert_equals(s.index.primary:select({}, {fullscan = true}),
                        {{1, 20}, {2, 10}})
        t.assert_equals(s.index.secondary:select({}, {fullscan = true}),
                        {{2, 10}, {1, 20}})
    end, {cg.params.engine})
end
