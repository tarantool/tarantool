local t = require('luatest')
local cluster = require('luatest.replica_set')
local decimal = require('decimal')

local g = t.group('gh-6377-decimal-compare-inf-crash',
                  {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_and_add_server({alias = 'master'})
    cg.master:start()
end)

g.after_all(function(cg)
    cg.cluster:stop()
end)

g.before_each(function(cg)
    local engine = cg.params.engine
    cg.master:exec(function(engine)
        box.schema.space.create('test', {engine = engine})
        box.space.test:create_index('pk', {
            parts = {{1, 'number'}},
            hint = false
        })
    end, {engine})
end)

g.after_each(function(cg)
    cg.master:exec(function()
        box.space.test:drop()
    end)
end)

local minf = -1 / 0
local inf = 1 / 0
local nan = 0 / 0
local val = decimal.new(1)

g.before_test('test_decimal_compare_inf_crash', function(cg)
    cg.master:exec(function(inf, minf, nan)
        box.space.test:insert{inf}
        box.space.test:insert{minf}
        box.space.test:insert{nan}
    end, {inf, minf, nan})
end)

g.test_decimal_compare_inf_crash = function(cg)
    local ordered_ret = {
        tostring(nan),
        tostring(minf),
        tostring(val),
        tostring(inf),
    }
    local ret = cg.master:exec(function(val)
        box.space.test:insert{val}
        local tab = box.space.test:select{}
        for k, v in pairs(tab) do
            tab[k] = tostring(v[1])
        end
        return tab
    end, {val})
    t.assert_equals(ret, ordered_ret, "Error in decimal comarison with Inf/NaN")
end

local mbig = -1e50
local big = 1e50

g.before_test('test_decimal_compare_big_float', function(cg)
    cg.master:exec(function(mbig, big)
        box.space.test:insert{mbig}
        box.space.test:insert{big}
    end, {mbig, big})
end)

g.test_decimal_compare_big_float = function(cg)
    local ordered_ret = {
        mbig,
        val,
        big,
    }
    local ret = cg.master:exec(function(val)
        box.space.test:insert{val}
        local tab = box.space.test:select{}
        for k, v in pairs(tab) do
            tab[k] = v[1]
        end
        return tab
    end, {val})
    t.assert_equals(ret, ordered_ret)
end
