local server = require('luatest.server')
local t = require('luatest')

local g = t.group("indexes", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

g.test_message_func_indexes = function(cg)
    cg.server:exec(function()
        -- Creating tables.
        box.execute("CREATE TABLE t1(id INTEGER PRIMARY KEY, a INTEGER);")
        box.execute([[CREATE TABLE t2(object INTEGER PRIMARY KEY, price INTEGER,
                                      count INTEGER);]])

        -- Expressions that're supposed to create functional indexes
        -- should return certain message.
        local _, err = box.execute("CREATE INDEX i1 ON t1(a + 1);")
        local exp_err = "Expressions are prohibited in an index definition"
        t.assert_equals(err.message, exp_err)
        local res = box.execute("CREATE INDEX i2 ON t1(a);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("CREATE INDEX i3 ON t2(price + 100);")
        t.assert_equals(err.message, exp_err)
        res = box.execute("CREATE INDEX i4 ON t2(price);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("CREATE INDEX i5 ON t2(count + 1);")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("CREATE INDEX i6 ON t2(count * price);")
        t.assert_equals(err.message, exp_err)

        -- Cleaning up.
        box.execute("DROP TABLE t1;")
        box.execute("DROP TABLE t2;")
    end)
end
