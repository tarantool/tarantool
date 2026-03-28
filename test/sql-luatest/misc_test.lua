local server = require('luatest.server')
local t = require('luatest')

local g = t.group("misc", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_misc = function(cg)
    cg.server:exec(function()
        -- Forbid multistatement queries.
        local res = box.execute('SELECT 1;')
        t.assert_equals(res.rows, {{1}})

        local exp_err = "At line 1 at or near position 11: keyword 'SELECT' "..
                        "is reserved. Please use double quotes "..
                        "if 'SELECT' is an identifier."
        local _, err = box.execute('SELECT 1; SELECT 2;')
        t.assert_equals(tostring(err), exp_err)

        local exp_err = "At line 1 at or near position 39: keyword 'SELECT' "..
                        "is reserved. Please use double quotes "..
                        "if 'SELECT' is an identifier."
        local sql = 'CREATE TABLE t1 (id INT PRIMARY KEY); SELECT 100;'
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        t.assert_equals(box.space.t1, nil)

        exp_err = "Failed to execute an empty SQL statement"
        _, err = box.execute(';')
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute('')
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute('     ;')
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute('\n\n\n\t\t\t   ')
        t.assert_equals(tostring(err), exp_err)
    end)
end

-- gh-3820: only table constraints can have a name.
g.test_3820_only_table_constraints_can_have_name = function(cg)
    cg.server:exec(function()
        local exp_err = "At line 1 at or near position 68: keyword 'NULL' "..
                        "is reserved. Please use double quotes if 'NULL' "..
                        "is an identifier."
        local sql = 'CREATE TABLE test (id INTEGER PRIMARY KEY, '..
                    'b INTEGER CONSTRAINT c1 NULL);'
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "At line 1 at or near position 68: "..
                  "keyword 'DEFAULT' is reserved. "..
                  "Please use double quotes if 'DEFAULT' is an identifier."
        sql = 'CREATE TABLE test (id INTEGER PRIMARY KEY, '..
              'b INTEGER CONSTRAINT c1 DEFAULT 300);'
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "At line 1 at or near position 65: "..
                  "keyword 'COLLATE' is reserved. "..
                  "Please use double quotes if 'COLLATE' is an identifier."
        sql = 'CREATE TABLE test (id INTEGER PRIMARY KEY, '..
              'b TEXT CONSTRAINT c1 COLLATE "binary");'
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        -- Make sure that type of literals in meta complies with its real
        -- type. For instance, typeof(0.5) is number, not integer.
        --
        local exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "integer",
                 },
            },
            rows = {{1}},
        }
        t.assert_equals(box.execute('SELECT 1;'), exp)

        exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "decimal",
                 },
            },
            rows = {{1.5}},
        }
        t.assert_equals(box.execute('SELECT 1.5;'), exp)

        exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "decimal",
                 },
            },
            rows = {{1.0}},
        }
        t.assert_equals(box.execute('SELECT 1.0;'), exp)

        exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "double",
                 },
            },
            rows = {{1.5}},
        }
        t.assert_equals(box.execute('SELECT 1.5e0;'), exp)

        exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "double",
                 },
            },
            rows = {{1.0}},
        }
        t.assert_equals(box.execute('SELECT 1e0;'), exp)

        exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "string",
                 },
            },
            rows = {{'abc'}},
        }
        t.assert_equals(box.execute([[SELECT 'abc';]]), exp)

        exp = {
            metadata = {
                 {
                    name = "COLUMN_1",
                    type = "varbinary",
                 },
            },
            rows = {{string.fromhex("4D6564766564")}},
        }
        t.assert_equals(box.execute([[SELECT X'4D6564766564';]]), exp)
    end)
end

-- gh-4139: assertion when reading a data-temporary space.
g.test_4139_assertion_reading_data_temporary_space = function(cg)
    cg.server:exec(function()
        local format = {{name = 'id', type = 'integer'}}
        local s = box.schema.space.create('s', {format=format, temporary=true})
        s:create_index('i')
        t.assert_equals(box.execute('SELECT * FROM "s";').rows, {})
        s:drop()
    end)
end

-- gh-4267: Full power of vdbe_field_ref
-- Tarantool's SQL internally stores data offset for all acceded
-- fields. It also keeps a bitmask of size 64 with all initialized
-- slots in actual state to find the nearest left field really
-- fast and parse tuple from that position. For fieldno >= 64
-- bitmask is not applicable, so it scans data offsets area in
-- a cycle.
--
-- The test below covers a case when this optimisation doesn't
-- work and the second lookup require parsing tuple from
-- beginning.
g.test_4267 = function(cg)
    cg.server:exec(function()
        local format = {}
        local tt = {}
        for i = 1, 70 do
                format[i] = {name = 'field'..i, type = 'unsigned'}
                tt[i] = i
        end
        local s = box.schema.create_space('test', {format = format})
        local pk = s:create_index('pk', {parts = {70}})
        s:insert(tt)

        local res = box.execute('SELECT field70, field64 FROM test;')
        t.assert_equals(res.rows, {{70, 64}})

        -- In the case below described optimization works fine.
        pk:alter({parts = {66}})
        res = box.execute('SELECT field66, field68, field70 FROM test;')
        t.assert_equals(res.rows, {{66, 68, 70}})
        box.space.test:drop()
    end)
end

-- gh-4933: Make sure that autoindex optimization is used.
g.test_4933_make_sure_autoindex_optimization_used = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t1(i INT PRIMARY KEY, a INT);')
        box.execute('CREATE TABLE t2(i INT PRIMARY KEY, b INT);')
        for i = 1, 10240 do
            box.execute('INSERT INTO t1 VALUES ($1, $1);', {i})
            box.execute('INSERT INTO t2 VALUES ($1, $1);', {i})
        end

        local exp = {
            metadata = {
                 {
                    name = "selectid",
                    type = "integer",
                 },
                 {
                    name = "order",
                    type = "integer",
                 },
                 {
                    name = "from",
                    type = "integer",
                 },
                 {
                    name = "detail",
                    type = "text",
                 },
            },
            rows = {
                {0, 0, 0, 'SCAN TABLE t1 (~1048576 rows)'},
                {
                    0, 1, 1,
                    'SEARCH TABLE t2 USING EPHEMERAL INDEX (b=?) (~20 rows)'
                },
            },
        }
        local sql = 'EXPLAIN QUERY PLAN SELECT a, b FROM t1, t2 WHERE a = b;'
        local res = box.execute(sql)
        t.assert_equals(res, exp)
    end)
end

-- gh-5592: Make sure that diag is not changed with the correct query.
g.test_5592_make_sure_diag_not_change_correct_query = function(cg)
    cg.server:exec(function()
        local exp_err = "Can't resolve field 'a'"
        local _, err = box.execute('SELECT a;')
        t.assert_equals(tostring(err), exp_err)

        local diag = box.error.last()
        local res = box.execute('SELECT * FROM (VALUES(true));')
        t.assert_equals(res.rows, {{true}})

        t.assert_equals(diag, box.error.last())

        -- exclude_null + SQL correctness.
        local sql = [[CREATE TABLE j (s1 INT PRIMARY KEY,
                      s2 STRING, s3 VARBINARY);]]
        box.execute(sql)
        local s = box.space.j
        local exp = {parts = {2, 'string', exclude_null = true}}
        box.space.j:create_index('I3', exp)
        box.execute([[INSERT INTO j VALUES (1,NULL,NULL), (2,'',X'00');]])

        exp = {
            {1, nil, nil},
            {2, '', string.fromhex("00")},
        }
        res = box.execute([[SELECT * FROM j;]])
        t.assert_equals(res.rows, exp)

        res = box.execute([[SELECT * FROM j INDEXED BY I3;]])
        t.assert_equals(res.rows, {{2, '', string.fromhex("00")}})

        res = box.execute([[SELECT COUNT(*) FROM j GROUP BY s2;]])
        t.assert_equals(res.rows, {{1}, {1}})

        res = box.execute([[SELECT COUNT(*) FROM j INDEXED BY I3;]])
        t.assert_equals(res.rows, {{1}})

        box.execute([[UPDATE j INDEXED BY I3 SET s2 = NULL;]])
        box.execute([[INSERT INTO j VALUES (3, 'a', X'33');]])

        exp = {
            {1, nil, nil},
            {2, nil, string.fromhex("00")},
            {3, 'a', string.fromhex("33")},
        }
        t.assert_equals(box.execute([[SELECT * FROM j;]]).rows, exp)
        res = box.execute([[SELECT * FROM j INDEXED BY I3;]])
        t.assert_equals(res.rows, {{3, 'a', string.fromhex("33")}})

        box.execute([[UPDATE j INDEXED BY I3 SET s3 = NULL;]])

        exp = {
            {1, nil, nil},
            {2, nil, string.fromhex("00")},
            {3, 'a', nil},
        }
        t.assert_equals(s:select{}, exp)

        s:drop()
    end)
end
