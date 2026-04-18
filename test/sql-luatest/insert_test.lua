local server = require('luatest.server')
local t = require('luatest')

local g = t.group("insert", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- gh-3565: INSERT OR REPLACE causes assertion fault.
g.test_3565_insert_replace_assertion_fault = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE tj (s1 INT PRIMARY KEY, s2 INT);")
        box.execute("INSERT INTO tj VALUES (1, 2), (2, 3);")
        box.execute("CREATE UNIQUE INDEX i ON tj (s2);")
        box.execute("REPLACE INTO tj VALUES (1, 3);")

        local res = box.execute("SELECT * FROM tj;")
        t.assert_equals(res.rows, {{1, 3}})
        box.execute("INSERT INTO tj VALUES (2, 4), (3, 5);")
        box.execute("UPDATE OR REPLACE tj SET s2 = s2 + 1;")

        local exp = {
            {1, 4},
            {3, 6},
        }
        res = box.execute("SELECT * FROM tj;")
        t.assert_equals(res.rows, exp)

        box.execute("DROP TABLE tj;")
    end)
end
