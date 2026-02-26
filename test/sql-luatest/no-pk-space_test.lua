local server = require('luatest.server')
local t = require('luatest')

local g = t.group("space", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--This code checks that basic SQL query operations
--do not work without a primary key
g.test_no_pk_space_test = function(cg)
    cg.server:exec(function()
        local format = {}
        format[1] = {'id', 'integer'}
        local s = box.schema.create_space('test', {format = format})

        local exp_err = [[SQL does not support spaces without primary key]]
        local _, err = box.execute([[SELECT * FROM "test";]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[INSERT INTO "test" VALUES (1);]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[DELETE FROM "test";]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[UPDATE "test" SET id = 3;]])
        t.assert_equals(tostring(err), exp_err)

        s:drop()
    end)
end
