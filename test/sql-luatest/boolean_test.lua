local server = require('luatest.server')
local t = require('luatest')

local g = t.group("boolean", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

--
-- gh-4697: Make sure that boolean precedes any number within
-- scalar. Result with order by indexed (using index) and
-- non-indexed (using no index) must be the same.
--
g.test_4697_scalar_bool_sort_cmp = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE test (s1 INTEGER PRIMARY KEY,
                                         s2 SCALAR UNIQUE, s3 SCALAR);]])
        box.execute([[INSERT INTO test VALUES (0, 1, 1);]])
        box.execute([[INSERT INTO test VALUES (1, 1.1, 1.1);]])
        box.execute([[INSERT INTO test VALUES (2, True, True);]])
        box.execute([[INSERT INTO test VALUES (3, NULL, NULL);]])

        local exp = {
            {nil, 'NULL'},
            {true, 'scalar'},
            {1, 'scalar'},
            {1.1, 'scalar'},
        }
        local sql = "SELECT s2, TYPEOF(s2) FROM SEQSCAN test ORDER BY s2;"
        t.assert_equals(box.execute(sql).rows, exp)
        sql = "SELECT s3, TYPEOF(s3) FROM SEQSCAN test ORDER BY s3;"
        t.assert_equals(box.execute(sql).rows, exp)
    end)
end

g.test_boolean = function(cg)
    cg.server:exec(function()
        local function execute(...)
            local res, err = box.execute(...)
            if err ~= nil then
                box.error(err)
            end
            return res
        end

        -- Create table for tests
        execute([[CREATE TABLE t (a BOOLEAN PRIMARY KEY);]])
        execute([[INSERT INTO t VALUES (true), (false);]])

        local func_return_type = {
            language = 'Lua',
            is_deterministic = true,
            body = 'function(x) return type(x) end',
            returns = 'string',
            param_list = {'any'},
            exports = {'LUA', 'SQL'},
        }
        box.schema.func.create('return_type', func_return_type)

        local func_return_type = {
            language = 'Lua',
            is_deterministic = true,
            body = "function(x) return type(x) == 'boolean' end",
            returns = 'boolean',
            param_list = {'any'},
            exports = {'LUA', 'SQL'},
        }
        box.schema.func.create('is_boolean', func_return_type)

        -- Check boolean as WHERE argument.
        local res = execute([[SELECT a FROM t WHERE a;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT a FROM t WHERE a != true;]])
        t.assert_equals(res.rows, {{false}})

        -- Check DEFAULT values for boolean.
        local sql = [[CREATE TABLE t0def (i INT PRIMARY KEY,
                      a BOOLEAN DEFAULT true);]]
        execute(sql)
        execute([[INSERT INTO t0def(i) VALUES (1);]])
        res = execute([[SELECT * FROM t0def;]])
        t.assert_equals(res.rows, {{1, true}})

        execute([[DROP TABLE t0def;]])

        -- Check UNKNOWN value for boolean.
        execute([[CREATE TABLE t0 (i INT PRIMARY KEY, a BOOLEAN);]])
        execute([[INSERT INTO t0 VALUES (1, false);]])
        execute([[INSERT INTO t0 VALUES (2, true);]])
        execute([[INSERT INTO t0 VALUES (3, NULL);]])
        execute([[INSERT INTO t0 VALUES (4, UNKNOWN);]])

        local exp = {
            {1, false},
            {2, true},
            {3, nil},
            {4, nil},
        }
        res = execute([[SELECT * FROM t0;]])
        t.assert_equals(res.rows, exp)

        -- Make sure that SCALAR can handle boolean values.
        sql = [[CREATE TABLE ts (id INT PRIMARY KEY AUTOINCREMENT,
                s SCALAR);]]
        execute(sql)
        execute([[INSERT INTO ts SELECT * FROM t0;]])

        res = execute([[SELECT s FROM ts WHERE s = true;]])
        t.assert_equals(res.rows, {{true}})

        execute([[INSERT INTO ts(s) VALUES ('abc'), (12.5);]])

        res = execute([[SELECT s FROM ts WHERE s = true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT s FROM ts WHERE s < true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT s FROM ts WHERE s IN (true, 1, 'abcd');]])
        t.assert_equals(res.rows, {{true}})

        --
        -- Make sure that BOOLEAN is not implicitly converted to INTEGER
        -- while inserted to PRIMARY KEY field.
        --
        local exp_err = 'Type mismatch: '..
                        'can not convert boolean(TRUE) to integer'
        local _, err = box.execute([[INSERT INTO ts VALUES (true, 12345);]])
        t.assert_equals(tostring(err), exp_err)

        -- Check that we can create index on field of type BOOLEAN.
        res = execute([[CREATE INDEX i0 ON t0(a);]])
        t.assert_equals(res.row_count, 1)

        -- Check boolean as LIMIT argument.
        exp_err = "Failed to execute SQL statement: "..
                  "Only positive integers are allowed in the LIMIT "..
                  "clause"
        _, err = box.execute([[SELECT * FROM t LIMIT true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT * FROM t LIMIT false;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check boolean as OFFSET argument.
        exp_err = "Failed to execute SQL statement: "..
                  "Only positive integers are allowed in the OFFSET "..
                  "clause"
        _, err = box.execute([[SELECT * FROM t LIMIT 1 OFFSET true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT * FROM t LIMIT 1 OFFSET false;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check involvance in index search.
        exp = {
            {
                0, 0, 0,
                "SEARCH TABLE t0 USING COVERING INDEX i0 (a=?) (~10 rows)"
            }
        }
        sql = [[EXPLAIN QUERY PLAN SELECT a FROM t0 WHERE a = true;]]
        res = execute(sql)
        t.assert_equals(res.rows, exp)

        local result = execute('EXPLAIN SELECT * FROM (VALUES(true)), t;')
        local i = 0
        for _,v in pairs(result.rows) do
            if (v[2] == 'OpenTEphemeral') then
                i = i + 1
            end
        end
        t.assert(i > 0)

        -- Check BOOLEAN as argument of user-defined function.
        exp = {
            {'boolean'},
            {'boolean'},
        }
        res = execute([[SELECT return_type(a) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT return_type('false');]])
        t.assert_equals(res.rows, {{'string'}})

        res = execute([[SELECT is_boolean(a) FROM t LIMIT 1;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT is_boolean('true');]])
        t.assert_equals(res.rows, {{false}})

        -- Check BOOLEAN as argument of scalar function.
        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function ABS()'
        _, err = box.execute([[SELECT ABS(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function LOWER()'
        _, err = box.execute([[SELECT LOWER(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function UPPER()'
        _, err = box.execute([[SELECT UPPER(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        exp = {
            {'FALSE'},
            {'TRUE'},
            {'NULL'},
            {'NULL'},
        }
        res = execute([[SELECT QUOTE(a) FROM t0;]])
        t.assert_equals(res.rows, exp)

        exp_err = 'Failed to execute SQL statement: '..
                        'wrong arguments for function LENGTH()'
        _, err = box.execute([[SELECT LENGTH(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        exp = {
            {'boolean'},
            {'boolean'},
            {'NULL'},
            {'NULL'},
        }
        res = execute([[SELECT TYPEOF(a) FROM t0;]])
        t.assert_equals(res.rows, exp)

        -- Check BOOLEAN as argument of aggregate function.
        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function AVG()'
        _, err = box.execute([[SELECT AVG(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT MIN(a) FROM t0;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT MAX(a) FROM t0;]])
        t.assert_equals(res.rows, {{true}})

        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function SUM()'
        _, err = box.execute([[SELECT SUM(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT COUNT(a) FROM t0;]])
        t.assert_equals(res.rows, {{2}})

        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function TOTAL()'
        _, err = box.execute([[SELECT TOTAL(a) FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Failed to execute SQL statement: '..
                  'wrong arguments for function GROUP_CONCAT()'
        _, err = box.execute([[SELECT GROUP_CONCAT(a, ' +++ ') FROM t0;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check BOOLEAN as binding parameter.
        exp = {{true, false, 'boolean', 'boolean'}}
        sql = 'SELECT ?, ?, return_type($1), TYPEOF($2);'
        res = execute(sql, {true, false})
        t.assert_equals(res.rows, exp)

        local parameters = {}
        parameters[1] = {}
        parameters[1]['@value2'] = true
        parameters[2] = {}
        parameters[2][':value1'] = false

        exp = {{false, true}}
        res = execute('SELECT :value1, @value2;', parameters)
        t.assert_equals(res.rows, exp)

        -- Check interactions with CHECK constraint.
        sql = [[CREATE TABLE t1 (i INT PRIMARY KEY, a BOOLEAN,
                CONSTRAINT ck CHECK(a != true));]]
        execute(sql)
        execute([[INSERT INTO t1 VALUES (1, false);]])

        exp_err = "Check constraint 'ck' failed for a tuple"
        _, err = box.execute([[INSERT INTO t1 VALUES (2, true);]])
        t.assert_equals(tostring(err), exp_err)

        -- Check interactions with FOREIGN KEY constraint.
        execute([[CREATE TABLE t2 (a BOOLEAN PRIMARY KEY,
                      b BOOLEAN REFERENCES t2(a));]])

        exp_err = "Foreign key constraint 'fk_unnamed_t2_b_1' "..
                  "failed for field '2 (b)': foreign "..
                  "tuple was not found"
        _, err = box.execute([[INSERT INTO t2 VALUES (false, true)]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[INSERT INTO t2 VALUES (true, false)]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[INSERT INTO t2 VALUES (true, NULL)]])
        t.assert_equals(err, nil)

        _, err = box.execute([[INSERT INTO t2 VALUES (false, true)]])
        t.assert_equals(err, nil)

        -- Check interactions with UNIQUE constraint.
        execute([[CREATE TABLE t3 (i INT PRIMARY KEY, a BOOLEAN,
                      CONSTRAINT uq UNIQUE(a));]])
        execute([[INSERT INTO t3 VALUES (1, true)]])
        execute([[INSERT INTO t3 VALUES (2, false)]])

        exp_err = 'Duplicate key exists in unique index '..
                  '"uq" in space "t3" with old tuple - [1, true] '..
                  'and new tuple - [3, true]'
        _, err = box.execute([[INSERT INTO t3 VALUES (3, true)]])
        t.assert_equals(tostring(err), exp_err)

        exp_err =  'Duplicate key exists in unique index "uq" in space "t3" '..
                   'with old tuple - [2, false] '..
                   'and new tuple - [4, false]'
        _, err = box.execute([[INSERT INTO t3 VALUES (4, false)]])
        t.assert_equals(tostring(err), exp_err)

        -- Check CAST from BOOLEAN to the other types.
        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT CAST(true AS INTEGER);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT CAST(false AS INTEGER);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT CAST(true AS NUMBER);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT CAST(false AS NUMBER);]])
        t.assert_equals(tostring(err), exp_err)

        sql = [[SELECT CAST(true AS TEXT), CAST(false AS TEXT);]]
        res = execute(sql)
        t.assert_equals(res.rows, {{'TRUE', 'FALSE'}})

        sql = [[SELECT CAST(true AS BOOLEAN), CAST(false AS BOOLEAN);]]
        res = execute(sql)
        t.assert_equals(res.rows, {{true, false}})

        -- Check CAST to BOOLEAN from the other types.
        exp_err = 'Type mismatch: can not convert integer(100) to boolean'
        _, err = box.execute([[SELECT CAST(100 AS BOOLEAN);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(1) to boolean'
        _, err = box.execute([[SELECT CAST(1 AS BOOLEAN);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(0) to boolean'
        _, err = box.execute([[SELECT CAST(0 AS BOOLEAN);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(0.123) to boolean'
        _, err = box.execute([[SELECT CAST(0.123 AS BOOLEAN);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(0.0) to boolean'
        _, err = box.execute([[SELECT CAST(0.0 AS BOOLEAN);]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT CAST('true' AS BOOLEAN),
                            CAST('false' AS BOOLEAN);]])
        t.assert_equals(res.rows, {{true, false}})

        res = execute([[SELECT CAST('TRUE' AS BOOLEAN),
                            CAST('FALSE' AS BOOLEAN);]])
        t.assert_equals(res.rows, {{true, false}})

        -- Check usage in trigger.
        execute([[CREATE TABLE t4 (i INT PRIMARY KEY, a BOOLEAN);]])
        execute([[CREATE TABLE t5 (i INT PRIMARY KEY AUTOINCREMENT,
                      b BOOLEAN);]])
        execute([[CREATE TRIGGER r AFTER INSERT ON t4 FOR EACH ROW BEGIN
                      INSERT INTO t5(b) VALUES(true); END;]])
        execute([[INSERT INTO t4 VALUES (100, false);]])
        execute([[DROP TRIGGER r;]])

        res = execute([[SELECT * FROM t4;]])
        t.assert_equals(res.rows, {{100, false}})

        res = execute([[SELECT * FROM t5;]])
        t.assert_equals(res.rows, {{1, true}})

        -- Check UNION, UNION ALL AND INTERSECT.
        execute([[INSERT INTO t5 VALUES (100, false);]])

        exp = {
            {1, true},
            {100, false},
        }
        res = execute([[SELECT * FROM t4 UNION SELECT * FROM t5;]])
        t.assert_equals(res.rows, exp)

        exp ={
            {100, false},
            {1, true},
            {100, false},
        }
        res = execute([[SELECT * FROM t4 UNION ALL SELECT * FROM t5;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT * FROM t4 INTERSECT SELECT * FROM t5;]])
        t.assert_equals(res.rows, {{100, false}})

        -- Check SUBSELECT.
        execute([[INSERT INTO t5(b) SELECT a FROM t4;]])

        exp ={
            {1, true},
            {100, false},
            {101, false},
        }
        res = execute([[SELECT * FROM t5;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {100, 100, false, true},
            {100, 100, false, false},
            {100, 100, false, false},
        }
        res, err = execute([[SELECT * FROM
                                 (SELECT t4.i, t5.i, a, b
                                 FROM t4, t5 WHERE a = false OR b = true);]])
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, exp)

        -- Check VIEW.
        execute([[CREATE VIEW v AS SELECT b FROM t5;]])

        exp = {
            {true},
            {false},
            {false},
        }
        res = execute([[SELECT * FROM v;]])
        t.assert_equals(res.rows, exp)

        -- Check DISTINCT.
        exp = {
            {true},
            {false},
        }
        res = execute([[SELECT DISTINCT * FROM v;]])
        t.assert_equals(res.rows, exp)

        -- Check CTE.
        execute([[WITH temp(x, y) AS (VALUES (111, false)
                      UNION ALL VALUES (222, true))
                      INSERT INTO t4 SELECT * from temp;]])
        exp = {
            {100, false},
            {111, false},
            {222, true},
        }
        res = execute([[SELECT * FROM t4;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {1, false},
            {2, true},
            {3, false},
            {4, true},
            {5, false},
            {6, true},
            {7, false},
            {8, true},
            {9, false},
            {10, true},
        }
        res = execute([[WITH RECURSIVE cnt(x, y) AS (VALUES(1, false)
                            UNION ALL SELECT x+1, x % 2 == 1 FROM
                            cnt WHERE x<10) SELECT x, y FROM cnt;]])
        t.assert_equals(res.rows, exp)

        -- Check JOINs.
        exp = {
            {100, false, 100, false},
            {100, false, 101, false},
            {111, false, 100, false},
            {111, false, 101, false},
            {222, true, 1, true},
        }
        res = execute([[SELECT * FROM t4 JOIN t5 ON t4.a = t5.b;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT * FROM t4 LEFT JOIN t5 ON t4.a = t5.b;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT * FROM t4 INNER JOIN t5 ON t4.a = t5.b;]])
        t.assert_equals(res.rows, exp)

        -- Check UPDATE.
        execute([[UPDATE t4 SET a = NOT a;]])

        exp = {
            {100, true},
            {111, true},
            {222, false},
        }
        res = execute([[SELECT * FROM t4;]])
        t.assert_equals(res.rows, exp)

        -- Check SWITCH-CASE.
        exp = {
            {100, true},
            {111, false},
            {222, false},
        }
        res = execute([[SELECT i, CASE WHEN a == true AND
                            i % 2 == 1 THEN false
                            WHEN a == true and i % 2 == 0 THEN true
                            WHEN a != true then false
                            END AS a0 FROM t4;]])
        t.assert_equals(res.rows, exp)

        -- Check ORDER BY.
        exp = {
            {100, false},
            {101, false},
            {222, false},
            {1, true},
            {100, true},
            {111, true},
        }
        res = execute([[SELECT * FROM t4 UNION SELECT * FROM t5
                            ORDER BY a, i;]])
        t.assert_equals(res.rows, exp)

        -- Check GROUP BY.
        exp = {
            {false, 3},
            {true, 3},
        }
        res = execute([[SELECT a, COUNT(*) FROM (SELECT * FROM t4
                            UNION SELECT * FROM t5) GROUP BY a;]])
        t.assert_equals(res.rows, exp)

        -- Check logical, bitwise and arithmetical operations.
        execute([[CREATE TABLE t6 (a1 BOOLEAN PRIMARY KEY, a2 BOOLEAN);]])
        execute([[INSERT INTO t6 VALUES (true, false), (false, true);]])

        res = execute([[SELECT NOT true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT NOT false;]])
        t.assert_equals(res.rows, {{true}})

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, NOT a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT true AND true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true AND false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false AND true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false AND false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true OR true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true OR false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false OR true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false OR false;]])
        t.assert_equals(res.rows, {{false}})

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, true AND a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, false AND a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, true OR a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, false OR a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a AND true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, a AND false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, a OR true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, a OR false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, false},
            {true, false, false},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a AND a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, true},
            {true, false, true},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a OR a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT -true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true * true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true * false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true / true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true / false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer, '..
                  'decimal or double'
        _, err = box.execute([[SELECT -false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT -a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false * true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false * false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false / true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false / false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer, '..
                  'decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT true + true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true + false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true - true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true - false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT false + true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false + false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false - true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false - false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT true % true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true % false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT false % true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false % false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a, true + a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true - a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a, false + a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false - a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a + true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a + false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a - true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a - false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a, true * a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true / a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a, false * a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false / a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a * true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a * false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a / true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a / false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT a, true % a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT a, false % a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a % true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a % false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a, a1, a + a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a1, a - a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a, a1, a * a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a1, a / a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT a, a1, a % a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to unsigned'
        _, err = box.execute([[SELECT ~true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true & true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false & true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true | true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false | true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true << true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false << true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true >> true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false >> true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to unsigned'
        _, err = box.execute([[SELECT ~false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false & false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true & false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false | false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true | false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false << false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true << false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true >> false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false >> false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true & a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false & a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true | a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false | a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true << a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false << a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true >> a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false >> a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to unsigned'
        _, err = box.execute([[SELECT a, a >> true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a & true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a | true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a << true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to unsigned'
        _, err = box.execute([[SELECT a, a >> false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a << false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a | false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a1, a & a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a & false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a1, a | a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a1, a << a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a1, a >> a1 FROM t, t6;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check concatenate.
        exp_err = 'Inconsistent types: expected string or '..
                  'varbinary got boolean(TRUE)'
        _, err = box.execute([[SELECT true || true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false || true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a || true FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 || a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 || a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a2, a2 || a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Inconsistent types: expected string or '..
                  'varbinary got boolean(FALSE)'
        _, err = box.execute([[SELECT true || false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false || false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, true || a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, false || a FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a, a || false FROM t;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 || a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a2, a1 || a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check comparisons.
        res = execute([[SELECT true > true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true > false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false > true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false > false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true < true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true < false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false < true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false < false;]])
        t.assert_equals(res.rows, {{false}})

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, true > a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, false > a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, true < a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a > true FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a < false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, false < a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a > false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, a < true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, false},
            {true, false, true},
            {true, true, false},
        }
        res = execute([[SELECT a, a1, a > a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, true},
            {true, false, false},
            {true, true, false},
        }
        res = execute([[SELECT a, a1, a < a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT true >= true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true >= false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false >= true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false >= false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true <= true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true <= false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false <= true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false <= false;]])
        t.assert_equals(res.rows, {{true}})

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, true >= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, false <= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a >= false FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a <= true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, false >= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a <= false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, true <= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a >= true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, true},
            {false, true, false},
            {true, false, true},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a >= a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, true},
            {false, true, true},
            {true, false, false},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a <= a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT true == true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true == false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false == true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false == false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true != true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true != false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false != true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false != false;]])
        t.assert_equals(res.rows, {{false}})

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, true == a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, false != a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a == true FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a != false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, false == a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, true != a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a == false FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a != true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, true},
            {false, true, false},
            {true, false, false},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a == a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, true},
            {true, false, true},
            {true, true, false},
        }
        res = execute([[SELECT a, a1, a != a1 FROM t, t6;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT true IN (true);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false IN (true);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true IN (false);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false IN (false);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true IN (true, false);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false IN (true, false);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true IN (SELECT a1 FROM t6);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false IN (SELECT a1 FROM t6);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true IN (SELECT a1 FROM t6 LIMIT 1);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false IN (SELECT a1 FROM t6 LIMIT 1);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true IN (1, 1.2, 'true', false);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false IN (1, 1.2, 'true', false);]])
        t.assert_equals(res.rows, {{true}})

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, a IN (true) FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, a IN (false) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a IN (SELECT a1 FROM
                            t6 LIMIT 1) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a IN (1, 1.2, 'true', false) FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, a IN (true, false) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a IN (SELECT a1 FROM t6) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT true BETWEEN true AND true;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false BETWEEN true AND true;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true BETWEEN false AND false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false BETWEEN false AND false;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT true BETWEEN true AND false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false BETWEEN true AND false;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT true BETWEEN false AND true;]])
         t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT false BETWEEN false AND true;]])
         t.assert_equals(res.rows, {{true}})

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, a BETWEEN true AND true FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, a BETWEEN false AND false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, a BETWEEN true AND false FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, a BETWEEN false AND true FROM t;]])
        t.assert_equals(res.rows, exp)

        -- Check interaction of BOOLEAN and INTEGER.
        execute([[CREATE TABLE t7 (b INT PRIMARY KEY);]])
        execute([[INSERT INTO t7 VALUES (123);]])

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT true AND 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true OR 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 2;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT 2 AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 2 OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 OR false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT a1, a1 AND 2 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 2 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 AND a1 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 OR a1 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 2 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 2 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 AND a2 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 OR a2 FROM t6]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT b, true AND b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true OR b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false OR b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b AND true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT b, false AND b FROM t7;]])
        t.assert_equals(res.rows, {{123, false}})

        res = execute([[SELECT b, b AND false FROM t7;]])
        t.assert_equals(res.rows, {{123, false}})

        _, err = box.execute([[SELECT b, b OR true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b OR false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 AND b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 OR b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b AND a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b OR a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 AND b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 OR b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b AND a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b OR a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT true + 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true - 2;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT false + 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false - 2;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT true * 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true / 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 + true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 - true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 * true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 / true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT false * 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false / 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 + false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 - false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 * false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 / false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT true % 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 % true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT false % 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 % false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a1, a1 + 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 - 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a1, a1 * 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 / 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 + a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 - a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 * a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 / a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT a1, a1 % 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 % a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a2, a2 + 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 - 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a2, a2 * 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 / 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 + a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 - a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 * a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 / a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT a2, a2 % 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 % a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT b, true + b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true - b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT b, false + b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false - b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT b, true * b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true / b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b + true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b - true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b * true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b / true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT b, false * b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false / b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b + false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b - false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b * false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b / false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT b, true % b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b % true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT b, false % b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b % false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a1, b, a1 + b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 - b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a1, b, a1 * b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 / b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b + a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b - a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b * a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b / a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT a1, b, a1 % b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b % a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a2, b, a2 + b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 - b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a2, b, a2 * b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 / b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b + a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b - a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b * a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b / a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT a2, b, a2 % b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b % a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to unsigned'
        _, err = box.execute([[SELECT true & 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true | 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true << 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true >> 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 & true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 | true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 << true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 >> true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to unsigned'
        _, err = box.execute([[SELECT false & 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false | 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false << 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false >> 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 & false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 | false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 << false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 >> false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to unsigned'
        _, err = box.execute([[SELECT a1, a1 & 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 | 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 << 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 >> 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 & a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 | a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 << a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 >> a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to unsigned'
        _, err = box.execute([[SELECT a2, a2 & 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 | 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 << 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 >> 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 & a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 | a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 << a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 >> a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to unsigned'
        _, err = box.execute([[SELECT b, true & b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true | b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true << b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true >> b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b & true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b | true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b << true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b >> true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to unsigned'
        _, err = box.execute([[SELECT b, false & b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false | b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false << b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false >> b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b & false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b | false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b << false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b >> false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to unsigned'
        _, err = box.execute([[SELECT a1, b, a1 & b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 | b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 << b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 >> b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b & a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b | a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b << a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b >> a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to unsigned'
        _, err = box.execute([[SELECT a2, b, a2 & b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 | b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 << b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 >> b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b & a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b | a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b << a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b >> a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT true > 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false > 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true < 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false < 2;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT 2 > true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 < true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT 2 > false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 < false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT a1, a1 > 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 < 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 > 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 < 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, 2 > a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 < a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, 2 > a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 < a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT b, true > b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false > b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true < b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false < b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT b, b > true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b < true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT b, b > false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b < false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT a1, b, a1 > b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 < b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 > b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 < b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, b, b > a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b < a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, b, b > a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b < a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT true >= 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false >= 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true <= 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false <= 2;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT 2 >= true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 <= true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT 2 >= false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 <= false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT a1, a1 >= 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 <= 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 >= 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 <= 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, 2 >= a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 <= a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, 2 >= a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 <= a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT b, true >= b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false >= b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true <= b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false <= b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT b, b >= true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b <= true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT b, b >= false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b <= false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT a1, b, a1 >= b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 <= b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 >= b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 <= b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, b, b >= a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b <= a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, b, b >= a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b <= a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT true == 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false == 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true != 2;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false != 2;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT 2 == true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 != true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT 2 == false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2 != false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(2) to boolean'
        _, err = box.execute([[SELECT a1, a1 == 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 != 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 == 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 != 2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, 2 == a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2 != a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, 2 == a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2 != a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT b, true == b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false == b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, true != b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, false != b FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT b, b == true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b != true FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT b, b == false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT b, b != false FROM t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert integer(123) to boolean'
        _, err = box.execute([[SELECT a1, b, a1 == b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, a1 != b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 == b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, a2 != b FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, b, b == a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, b, b != a1 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, b, b == a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, b, b != a2 FROM t6, t7;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT true IN (0, 1, 2, 3);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false IN (0, 1, 2, 3);]])
        t.assert_equals(res.rows, {{false}})

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT true IN (SELECT b FROM t7);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT false IN (SELECT b FROM t7);]])
        t.assert_equals(tostring(err), exp_err)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a1, a1 IN (0, 1, 2, 3) FROM t6;]])
        t.assert_equals(res.rows, exp)

        exp_err = 'Type mismatch: can not convert integer(0) to boolean'
        _, err = box.execute([[SELECT true BETWEEN 0 and 10;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false BETWEEN 0 and 10;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 BETWEEN 0 and 10 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 BETWEEN 0 and 10 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check interaction of BOOLEAN and DOUBLE.
        execute([[CREATE TABLE t8 (c DOUBLE PRIMARY KEY);]])
        execute([[INSERT INTO t8 VALUES (4.56);]])

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT true AND 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 2.3;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT true OR 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT 2.3 AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 2.3 OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 OR false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 AND 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 AND a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 OR a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 AND a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 OR a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT c, true AND c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT c, false AND c FROM t8;]])
        t.assert_equals(res.rows, {{4.56, false}})

        _, err = box.execute([[SELECT c, true OR c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false OR c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c AND true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT c, c AND false FROM t8;]])
        t.assert_equals(res.rows, {{4.56, false}})

        _, err = box.execute([[SELECT c, c OR true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c OR false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 AND c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 OR c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c AND a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c OR a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 AND c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 OR c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c AND a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c OR a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT true + 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true - 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT false + 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false - 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT true * 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true / 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 + true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 - true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 * true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 / true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT false * 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false / 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 + false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 - false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 * false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 / false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT true % 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert decimal(2.3) to integer"
        _, err = box.execute([[SELECT 2.3 % true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT false % 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert decimal(2.3) to integer"
        _, err = box.execute([[SELECT 2.3 % false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a1, a1 + 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 - 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a1, a1 * 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 / 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 + a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 - a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 * a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 / a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT a1, a1 % 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to integer'
        _, err = box.execute([[SELECT a1, 2.3 % a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a2, a2 + 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 - 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a2, a2 * 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 / 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 + a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 - a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 * a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 / a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT a2, a2 % 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to integer'
        _, err = box.execute([[SELECT a2, 2.3 % a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT c, true + c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, true - c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT c, false + c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false - c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT c, true * c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, true / c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c + true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c - true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c * true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c / true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT c, false * c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false / c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c + false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c - false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c * false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c / false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT c, true % c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT c, false % c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to integer'
        _, err = box.execute([[SELECT c, c % true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c % false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a1, c, a1 + c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 - c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a1, c, a1 * c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 / c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c + a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c - a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c * a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c / a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to integer'
        _, err = box.execute([[SELECT a1, c, a1 % c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to integer'
        _, err = box.execute([[SELECT a1, c, c % a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal, double, datetime '..
                  'or interval'
        _, err = box.execute([[SELECT a2, c, a2 + c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 - c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) '..
                  'to integer, decimal or double'
        _, err = box.execute([[SELECT a2, c, a2 * c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 / c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c + a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c - a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c * a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c / a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to integer'
        _, err = box.execute([[SELECT a2, c, a2 % c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to integer'
        _, err = box.execute([[SELECT a2, c, c % a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT true > 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false > 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true < 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false < 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT 2.3 > true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 < true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT 2.3 > false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 < false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT a1, a1 > 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 < 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 > 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 < 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, 2.3 > a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 < a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, 2.3 > a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 < a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT c, true > c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false > c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, true < c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false < c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT c, c > true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c < true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT c, c > false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c < false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT a1, c, a1 > c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 < c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 > c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 < c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, c, c > a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c < a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, c, c > a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c < a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT true >= 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false >= 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true <= 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false <= 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT 2.3 >= true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 <= true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT 2.3 >= false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 <= false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT a1, a1 >= 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 <= 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 >= 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 <= 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, 2.3 >= a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 <= a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, 2.3 >= a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 <= a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT c, true >= c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false >= c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, true <= c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false <= c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT c, c >= true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c <= true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT c, c >= false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c <= false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT a1, c, a1 >= c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 <= c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 >= c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 <= c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, c, c >= a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c <= a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, c, c >= a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c <= a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT true == 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false == 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true != 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false != 2.3;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT 2.3 == true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 != true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT 2.3 == false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 2.3 != false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(2.3) to boolean'
        _, err = box.execute([[SELECT a1, a1 == 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 != 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 == 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 != 2.3 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, 2.3 == a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 2.3 != a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, 2.3 == a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 2.3 != a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT c, true == c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false == c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, true != c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, false != c FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT c, c == true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c != true FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT c, c == false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT c, c != false FROM t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert double(4.56) to boolean'
        _, err = box.execute([[SELECT a1, c, a1 == c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, a1 != c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 == c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, a2 != c FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to number'
        _, err = box.execute([[SELECT a1, c, c == a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, c, c != a1 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to number'
        _, err = box.execute([[SELECT a2, c, c == a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, c, c != a2 FROM t6, t8;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT true IN (0.1, 1.2, 2.3, 3.4);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT false IN (0.1, 1.2, 2.3, 3.4);]])
        t.assert_equals(res.rows, {{false}})

        sql = [[SELECT a1 IN (0.1, 1.2, 2.3, 3.4) FROM t6 LIMIT 1;]]
        res = execute(sql)
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT a2 IN (0.1, 1.2, 2.3, 3.4)
                            FROM t6 LIMIT 1;]])
        t.assert_equals(res.rows, {{false}})

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to double'
        _, err = box.execute([[SELECT true IN (SELECT c FROM t8);]])
        t.assert_equals(tostring(err), exp_err)

        sql = [[SELECT a2 IN (SELECT c FROM t8)
                FROM t6 LIMIT 1;]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to double'
        _, err = box.execute([[SELECT false IN (SELECT c FROM t8);]])
        t.assert_equals(tostring(err), exp_err)

        sql = [[SELECT a1 IN (SELECT c FROM t8)
                FROM t6 LIMIT 1;]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert decimal(0.1) to boolean'
        _, err = box.execute([[SELECT true BETWEEN 0.1 and 9.9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false BETWEEN 0.1 and 9.9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 BETWEEN 0.1 and 9.9 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 BETWEEN 0.1 and 9.9 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        -- Check interaction of BOOLEAN and TEXT.
        execute([[CREATE TABLE t9 (d TEXT PRIMARY KEY);]])
        execute([[INSERT INTO t9 VALUES ('AsdF');]])

        exp_err = "Type mismatch: can not convert string('abc') to boolean"
        _, err = box.execute([[SELECT true AND 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 'abc';]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT true OR 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'abc' AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT 'abc' AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 'abc' OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'abc' OR false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 AND 'abc' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 'abc' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'abc' AND a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'abc' OR a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 'abc' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 'abc' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'abc' AND a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'abc' OR a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('AsdF') to boolean"
        _, err = box.execute([[SELECT d, true AND d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, false AND d FROM t9;]])
        t.assert_equals(res.rows, {{'AsdF', false}})

        _, err = box.execute([[SELECT d, true OR d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false OR d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d AND true FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, d AND false FROM t9;]])
        t.assert_equals(res.rows, {{'AsdF', false}})

        _, err = box.execute([[SELECT d, d OR true FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d OR false FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 AND d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 OR d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d AND a1 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d OR a1 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 AND d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 OR d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d AND a2 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d OR a2 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('abc') to boolean"
        _, err = box.execute([[SELECT true > 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false > 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT true < 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false < 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert boolean(TRUE) to string"
        _, err = box.execute([[SELECT 'abc' > true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'abc' < true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert boolean(FALSE) to string"
        _, err = box.execute([[SELECT 'abc' > false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'abc' < false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('AsdF') to boolean"
        _, err = box.execute([[SELECT d, true > d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false > d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, true < d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false < d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to string'
        _, err = box.execute([[SELECT d, d > true FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d < true FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to string'
        _, err = box.execute([[SELECT d, d > false FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d < false FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('AsdF') to boolean"
        _, err = box.execute([[SELECT a1, d, a1 > d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 < d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 > d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 < d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(FALSE) to string'
        _, err = box.execute([[SELECT a1, d, d > a1 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d < a1 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Type mismatch: can not convert boolean(TRUE) to string'
        _, err = box.execute([[SELECT a2, d, d > a2 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d < a2 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Inconsistent types: '..
                  'expected string or varbinary got boolean(TRUE)'
        _, err = box.execute([[SELECT true || 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'abc' || true;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Inconsistent types: '..
                  'expected string or varbinary got boolean(FALSE)'
        _, err = box.execute([[SELECT false || 'abc';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'abc' || false;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Inconsistent types: '..
                  'expected string or varbinary got boolean(TRUE)'
        _, err = box.execute([[SELECT d, true || d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d || true FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Inconsistent types: '..
                  'expected string or varbinary got boolean(FALSE)'
        _, err = box.execute([[SELECT d, false || d FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d || false FROM t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, a1 || d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d || a1 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = 'Inconsistent types: '..
                  'expected string or varbinary got boolean(TRUE)'
        _, err = box.execute([[SELECT d, a2 || d FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d || a2 FROM t6, t9;]])
        t.assert_equals(tostring(err), exp_err)

        --
        -- Check special types of strings: 'TRUE', 'true', 'FALSE',
        -- 'false'.
        --
        execute([[INSERT INTO t9 VALUES
                      ('TRUE'), ('true'), ('FALSE'), ('false');]])

        exp_err = "Type mismatch: can not convert string('TRUE') to boolean"
        _, err = box.execute([[SELECT true AND 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 'TRUE';]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT true OR 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'TRUE' AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT 'TRUE' AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 'TRUE' OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'TRUE' OR false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 AND 'TRUE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 'TRUE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'TRUE' AND a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'TRUE' OR a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 'TRUE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 'TRUE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'TRUE' AND a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'TRUE' OR a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, true AND
                               d FROM t9 WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, false AND d FROM t9 WHERE d = 'TRUE';]])
        t.assert_equals(res.rows, {{'TRUE', false}})

        _, err = box.execute([[SELECT d, true OR d FROM t9 WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false OR d FROM t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d AND true FROM
                               t9 WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, d AND false FROM t9 WHERE d = 'TRUE';]])
        t.assert_equals(res.rows, {{'TRUE', false}})

        _, err = box.execute([[SELECT d, d OR true FROM t9 WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d OR false FROM t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 AND d FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 OR d FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d AND a1 FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d OR a1 FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 AND d FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 OR d FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d AND a2 FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d OR a2 FROM t6, t9
                               WHERE d = 'TRUE';]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('true') to boolean"
        _, err = box.execute([[SELECT true AND 'true';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 'true';]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT true OR 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'true' AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT 'true' AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 'true' OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'true' OR false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 AND 'true' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 'true' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'true' AND a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'true' OR a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 'true' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 'true' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'true' AND a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'true' OR a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, true AND d FROM t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, false AND d FROM t9 WHERE d = 'true';]])
        t.assert_equals(res.rows, {{'true', false}})

        _, err = box.execute([[SELECT d, true OR d FROM t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false OR d FROM t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d AND true FROM t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, d AND false FROM t9
                            WHERE d = 'true';]])
        t.assert_equals(res.rows, {{'true', false}})

        _, err = box.execute([[SELECT d, d OR true FROM t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d OR false FROM t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 AND d FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 OR d FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d AND a1 FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d OR a1 FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 AND d FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 OR d FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d AND a2 FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d OR a2 FROM t6, t9
                               WHERE d = 'true';]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('FALSE') to boolean"
        _, err = box.execute([[SELECT true AND 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 'FALSE';]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT true OR 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'FALSE' AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT 'FALSE' AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 'FALSE' OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'FALSE' OR false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 AND 'FALSE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 'FALSE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'FALSE' AND a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'FALSE' OR a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 'FALSE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 'FALSE' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'FALSE' AND a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'FALSE' OR a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, true AND d FROM t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, false AND d FROM t9 WHERE d = 'FALSE';]])
        t.assert_equals(res.rows, {{'FALSE', false}})

        _, err = box.execute([[SELECT d, true OR d FROM t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false OR d FROM t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d AND true FROM t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, d AND false FROM t9
                            WHERE d = 'FALSE';]])
        t.assert_equals(res.rows, {{'FALSE', false}})

        _, err = box.execute([[SELECT d, d OR true FROM t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d OR false FROM t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 AND d FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 OR d FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d AND a1 FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d OR a1 FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 AND d FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 OR d FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d AND a2 FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d OR a2 FROM t6, t9
                               WHERE d = 'FALSE';]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Type mismatch: can not convert string('false') to boolean"
        _, err = box.execute([[SELECT true AND 'false';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT false AND 'false';]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT true OR 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT false OR 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'false' AND true;]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT 'false' AND false;]])
        t.assert_equals(res.rows, {{false}})

        _, err = box.execute([[SELECT 'false' OR true;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT 'false' OR false;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 AND 'false' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, a1 OR 'false' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'false' AND a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, 'false' OR a1 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 AND 'false' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, a2 OR 'false' FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'false' AND a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, 'false' OR a2 FROM t6;]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, true AND d FROM t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, false AND d FROM t9
                            WHERE d = 'false';]])
        t.assert_equals(res.rows, {{'false', false}})

        _, err = box.execute([[SELECT d, true OR d FROM t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, false OR d FROM t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d AND true FROM t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        res = execute([[SELECT d, d AND false FROM t9
                            WHERE d = 'false';]])
        t.assert_equals(res.rows, {{'false', false}})

        _, err = box.execute([[SELECT d, d OR true FROM t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT d, d OR false FROM t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 AND d FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, a1 OR d FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d AND a1 FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a1, d, d OR a1 FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 AND d FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, a2 OR d FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d AND a2 FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a2, d, d OR a2 FROM t6, t9
                               WHERE d = 'false';]])
        t.assert_equals(tostring(err), exp_err)

        -- Cleaning.
        execute([[DROP VIEW v;]])
        execute([[DROP TABLE t9;]])
        execute([[DROP TABLE t8;]])
        execute([[DROP TABLE t7;]])
        execute([[DROP TABLE t6;]])
        execute([[DROP TABLE t5;]])
        execute([[DROP TABLE t4;]])
        execute([[DROP TABLE t3;]])
        execute([[DROP TABLE t2;]])
        execute([[DROP TABLE t1;]])
        execute([[DROP TABLE t0;]])
        execute([[DROP TABLE ts;]])
        execute([[DROP TABLE t;]])

        execute([[DELETE FROM "_func" WHERE "name" = 'check_t1_ck';]])
        execute([[DELETE FROM "_func" WHERE "name" = 'return_type';]])
        execute([[DELETE FROM "_func" WHERE "name" = 'is_boolean';]])
    end)
end
