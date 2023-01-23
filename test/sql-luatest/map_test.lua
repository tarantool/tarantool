local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'map'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function()
    g.server:stop()
end)

--- Make sure syntax for MAP values works as intended.
g.test_map_1_1 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : 123};]]
        local res = {{{a = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_2 = function()
    g.server:exec(function()
        local dec = require('decimal')
        local sql = [[SELECT {'a' : 123.1};]]
        local res = {{{a = dec.new('123.1')}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_3 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : 123.1e0};]]
        local res = {{{a = 123.1}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_4 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : CAST(123 AS NUMBER)};]]
        local res = {{{a = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_5 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : '123'};]]
        local res = {{{a = '123'}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_6 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : x'313233'};]]
        local res = {{{a = '123'}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_7 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : true};]]
        local res = {{{a = true}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_8 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : CAST(123 AS SCALAR)};]]
        local res = {{{a = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_9 = function()
    g.server:exec(function()
        local uuid = require('uuid')
        local sql = [[SELECT {'a' : ]]..
                    [[CAST('11111111-1111-1111-1111-111111111111' AS UUID)};]]
        local res = {a = uuid.fromstr('11111111-1111-1111-1111-111111111111')}
        t.assert_equals(box.execute(sql).rows[1][1], res)
    end)
end

g.test_map_1_10 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : [1, 2, 3]};]]
        local res = {{{a = {1, 2, 3}}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_1_11 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : {'a': 1, 'b' : 2, 'c' : 3}};]]
        local res = {{{a = {a = 1, b = 2, c = 3}}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure MAP() accepts only INTEGER, STRING and UUID as keys.
g.test_map_2_1 = function()
    g.server:exec(function()
        local sql = [[SELECT {123 : 123};]]
        local res = {{{[123] = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_2_2 = function()
    g.server:exec(function()
        local sql = [[SELECT {123.1 : 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_3 = function()
    g.server:exec(function()
        local sql = [[SELECT {123.1e0 : 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_4 = function()
    g.server:exec(function()
        local sql = [[SELECT {CAST(123 AS NUMBER) : 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_5 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : 123};]]
        local res = {{{a = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_2_6 = function()
    g.server:exec(function()
        local sql = [[SELECT {x'313233' : 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_7 = function()
    g.server:exec(function()
        local sql = [[SELECT {true : 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_8 = function()
    g.server:exec(function()
        local sql = [[SELECT {CAST(123 AS SCALAR) : 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_9 = function()
    g.server:exec(function()
        local uuid = require('uuid')
        local sql = [[SELECT {CAST('11111111-1111-1111-1111-111111111111' ]]..
                    [[AS UUID) : 123}]]
        local res = uuid.fromstr('11111111-1111-1111-1111-111111111111')
        local val, _ = next(box.execute(sql).rows[1][1])
        t.assert_equals(val, res);
    end)
end

g.test_map_2_10 = function()
    g.server:exec(function()
        local sql = [[SELECT {[1, 2, 3]: 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_map_2_11 = function()
    g.server:exec(function()
        local sql = [[SELECT {{'a': 1, 'b' : 2, 'c' : 3}: 123};]]
        local res = [[Only integer, string and uuid can be keys in map]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure expressions can be used as a key or a value in the MAP constructor.
g.test_map_3_1 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : a} FROM (SELECT 123 AS a);]]
        local res = {{{a = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_3_2 = function()
    g.server:exec(function()
        local sql = [[SELECT {a : 123} FROM (SELECT 123 AS a);]]
        local res = {{{[123] = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_3_3 = function()
    g.server:exec(function()
        local sql = [[SELECT {1 + 2 : 123};]]
        local res = {{{[3] = 123}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_map_3_4 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : 1 + 2};]]
        local res = {{{a = 3}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure TYPEOF() properly works with the MAP constructor.
g.test_map_4 = function()
    g.server:exec(function()
        local sql = [[SELECT typeof({'a' : 123});]]
        local res = {{'map'}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure PRINTF() properly works with the MAP constructor.
g.test_map_5 = function()
    g.server:exec(function()
        local sql = [[SELECT printf({});]]
        local res = {{'{}'}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that the MAP constructor can create big MAP values.
g.test_map_6 = function()
    g.server:exec(function()
        local map = {['c0'] = 0}
        local str = "'c0': 0"
        for i = 1, 1000 do
            map['c'..tostring(i)] = i
            str = str .. string.format(", 'c%d': %d", i, i)
        end
        local sql = [[SELECT {]]..str..[[};]]
        t.assert_equals(box.execute(sql).rows[1][1], map)
    end)
end

-- Make sure symbol ':' is properly processed by parser.
g.test_map_7_1 = function()
    g.server:exec(function()
        local sql = [[SELECT {:name};]]
        local res = [[Syntax error at line 1 near '}']]
        local _, err = box.execute(sql, {{[':name'] = 1}})
        t.assert_equals(err.message, res)
    end)
end

g.test_map_7_2 = function()
    g.server:exec(function()
        local sql = [[SELECT {:name : 5};]]
        local res = {{{[1] = 5}}}
        t.assert_equals(box.execute(sql, {{[':name'] = 1}}).rows, res)
    end)
end

g.test_map_7_3 = function()
    g.server:exec(function()
        local sql = [[SELECT {5::name};]]
        local res = {{{[5] = 1}}}
        t.assert_equals(box.execute(sql, {{[':name'] = 1}}).rows, res)
    end)
end

-- Make sure that multiple row with maps can be selected properly.
g.test_map_8 = function()
    g.server:exec(function()
        local maps = {{{a1 = 1}}}
        local strs = "({'a1': 1})"
        for i = 2, 1000 do
            maps[i] = {{['a'..tostring(i)] = i}}
            strs = strs .. string.format(", ({'a%d': %d})", i, i)
        end
        local sql = [[SELECT * FROM (VALUES]]..strs..[[);]]
        t.assert_equals(box.execute(sql).rows, maps)
    end)
end

-- Make sure that the last of values with the same key is set.
g.test_map_9 = function()
    g.server:exec(function()
        local sql = [[SELECT {'a' : 1, 'a' : 2, 'b' : 3, 'a' : 4};]]
        local res = {{{b = 3, a = 4}}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end
