local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            -- Disable the tuple cache and bloom filters.
            vinyl_cache = 0,
            vinyl_bloom_fpr = 1,
        },
    })
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

g.test_exact_match_optimization = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('i1')
        s:create_index('i2', {
            unique = true,
            parts = {{2, 'unsigned'}},
        })
        s:create_index('i3', {
            unique = true,
            parts = {{3, 'unsigned', is_nullable = true}},
        })
        s:replace({10, 10, 10})
        s:replace({20, 20, 20})
        s:replace({30, 30, 30})
        s:replace({40, 40, 40})
        box.snapshot()
        s:delete({10})
        s:delete({20})
        s:replace({20, 20, 20})
        s:delete({30})
        s:replace({31, 30, 30})
        s:delete({40})
        s:replace({39, 40, 40})

        box.stat.reset()

        local function check(index, op, ret)
            local json = require('json')
            local msg = string.format("%s %s", index.name, json.encode(op))
            t.assert_equals(index[op[1]](index, unpack(op, 2)), ret, msg)
            t.assert_equals(index:stat().disk.iterator.lookup, 0, msg)
        end

        for i = 0, 2 do
            local index = s.index[i]

            check(index, {'get', {10}}, nil)
            check(index, {'select', {10}}, {})
            check(index, {'select', {10}, {iterator = 'req'}}, {})

            local tuple = {20, 20, 20}
            check(index, {'get', {20}}, tuple)
            check(index, {'select', {20}}, {tuple})
            check(index, {'select', {20}, {iterator = 'req'}}, {tuple})
            check(index, {'select', {20},
                  {iterator = 'ge', limit = 1}}, {tuple})
            check(index, {'select', {20},
                  {iterator = 'le', limit = 1}}, {tuple})

            if i > 0 then
                tuple = {31, 30, 30}
                check(index, {'get', {30}}, tuple)
                check(index, {'select', {30}}, {tuple})
                check(index, {'select', {30}, {iterator = 'req'}}, {tuple})
                check(index, {'select', {30},
                      {iterator = 'ge', limit = 1}}, {tuple})
                check(index, {'select', {30},
                      {iterator = 'le', limit = 1}}, {tuple})

                tuple = {39, 40, 40}
                check(index, {'get', {40}}, tuple)
                check(index, {'select', {40}}, {tuple})
                check(index, {'select', {40}, {iterator = 'req'}}, {tuple})
                check(index, {'select', {40},
                      {iterator = 'ge', limit = 1}}, {tuple})
                check(index, {'select', {40},
                      {iterator = 'le', limit = 1}}, {tuple})
            end
        end
    end)
end
