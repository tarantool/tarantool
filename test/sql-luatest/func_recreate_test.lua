local server = require('luatest.server')
local t = require('luatest')

local g = t.group("func", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check errors during function create process
g.test_func_recreate = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local _, err = box.schema.func.create('WAITFOR', {language = 'Lua',
                            body = [[function (n)
                                     require('fiber').sleep(n) return n
                                     end]],
                            param_list = {'number'}, returns = 'number',
                            exports = {'LUA', 'SQL'}})
        t.assert_equals(err, nil)
        local ch = fiber.channel(1)

        _, err = fiber.create(function () ch:put(box.execute('SELECT WAITFOR(0.2e0);')) end)
        t.assert_equals(err, nil)
        fiber.sleep(0.2)

        box.func.WAITFOR:drop()

        _, err = box.schema.func.create('WAITFOR', {language = 'Lua',
                            body = "function (n) require('fiber').sleep(n) return n end",
                            param_list = {'number'}, returns = 'number',
                            exports = {'LUA', 'SQL'}})
        t.assert_equals(err, nil)

        local res = ch:get()
        t.assert_equals(res.rows, {{0.2}})
        box.func.WAITFOR:drop()

        _, err = box.schema.func.create('WAITFOR', {language = 'Lua',
                        body = "function (n) require('fiber').sleep(n) return n end",
                        param_list = {'number'}, returns = 'number',
                        exports = {'LUA', 'SQL'}})
        t.assert_equals(err, nil)

        box.func.WAITFOR:drop()
    end)
end
