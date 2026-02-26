local server = require('luatest.server')
local t = require('luatest')

local g = t.group("func_change", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_5427_lua_func_changes_result = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (b STRING PRIMARY KEY);]])
        box.execute([[INSERT INTO t VALUES ('aaaaaaaaaaaa');]])
        box.schema.func.create('CORRUPT_SELECT', {
            returns = 'integer',
            body = [[
                function()
                    box.space.t:select()
                    return 1
                end]],
            exports = {'LUA', 'SQL'},
        });
        box.schema.func.create('CORRUPT_GET', {
            returns = 'integer',
            body = [[
                function()
                    box.space.t:get('aaaaaaaaaaaa')
                    return 1
                end]],
            exports = {'LUA', 'SQL'},
        });
        box.schema.func.create('CORRUPT_COUNT', {
            returns = 'integer',
            body = [[
                function()
                    box.space.t:count()
                    return 1
                end]],
            exports = {'LUA', 'SQL'},
        });
        box.schema.func.create('CORRUPT_MAX', {
            returns = 'integer',
            body = [[
                function()
                    box.space.t.index[0]:max()
                    return 1
                end]],
            exports = {'LUA', 'SQL'},
        });
        box.schema.func.create('CORRUPT_MIN', {
            returns = 'integer',
            body = [[
                function()
                    box.space.t.index[0]:min()
                    return 1
                end]],
            exports = {'LUA', 'SQL'},
        });

        local values = {"aaaaaaaaaaaa", "aaaaaaaaaaaa"}
        local query = [[SELECT %s() FROM t
                        WHERE t.b = ? AND t.b <= ? ORDER BY t.b;]]
        local res = box.execute(string.format(query, 'CORRUPT_SELECT'), values)
        local exp = {
            metadata = {
                {name = 'COLUMN_1', type = 'integer'},
            },
            rows = {{1}},
        }
        t.assert_equals(res, exp)
        res = box.execute(string.format(query, 'CORRUPT_GET'), values)
        t.assert_equals(res, exp)
        res = box.execute(string.format(query, 'CORRUPT_COUNT'), values)
        t.assert_equals(res, exp)
        res = box.execute(string.format(query, 'CORRUPT_MAX'), values)
        t.assert_equals(res, exp)
        res = box.execute(string.format(query, 'CORRUPT_MIN'), values)
        t.assert_equals(res, exp)
    end)
end
