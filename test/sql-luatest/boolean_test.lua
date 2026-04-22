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
        local function execute(...)
            local res, err = box.execute(...)
            if err ~= nil then
                box.error(err)
            end
            return res
        end
        local function assert_type_mismatch(sql)
            local exp_err = {name = "SQL_TYPE_MISMATCH"}
            t.assert_error_covers(exp_err, execute, sql)
        end
        rawset(_G, 'execute', execute)
        rawset(_G, 'assert_type_mismatch', assert_type_mismatch)
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
        local execute = _G.execute
        local assert_type_mismatch = _G.assert_type_mismatch

        -- Create table for tests
        execute([[CREATE TABLE t (a BOOLEAN PRIMARY KEY);]])
        execute([[INSERT INTO t VALUES (TRUE), (FALSE);]])

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

        res = execute([[SELECT a FROM t WHERE a != TRUE;]])
        t.assert_equals(res.rows, {{false}})

        -- Check DEFAULT values for boolean.
        local sql = [[CREATE TABLE t1 (i INT PRIMARY KEY,
                      a BOOLEAN DEFAULT TRUE);]]
        execute(sql)
        execute([[INSERT INTO t1(i) VALUES (1);]])
        res = execute([[SELECT * FROM t1;]])
        t.assert_equals(res.rows, {{1, true}})

        execute([[DROP TABLE t1;]])

        -- Check UNKNOWN value for boolean.
        execute([[CREATE TABLE t1 (i INT PRIMARY KEY, a BOOLEAN);]])
        execute([[INSERT INTO t1 VALUES (1, FALSE);]])
        execute([[INSERT INTO t1 VALUES (2, TRUE);]])
        execute([[INSERT INTO t1 VALUES (3, NULL);]])
        execute([[INSERT INTO t1 VALUES (4, UNKNOWN);]])

        local exp = {
            {1, false},
            {2, true},
            {3, nil},
            {4, nil},
        }
        res = execute([[SELECT * FROM t1;]])
        t.assert_equals(res.rows, exp)

        -- Make sure that SCALAR can handle boolean values.
        sql = [[CREATE TABLE t2 (id INT PRIMARY KEY AUTOINCREMENT,
                s SCALAR);]]
        execute(sql)
        execute([[INSERT INTO t2 SELECT * FROM t1;]])

        res = execute([[SELECT s FROM t2 WHERE s = TRUE;]])
        t.assert_equals(res.rows, {{true}})

        execute([[INSERT INTO t2(s) VALUES ('abc'), (12.5);]])

        res = execute([[SELECT s FROM t2 WHERE s = TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT s FROM t2 WHERE s < TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT s FROM t2 WHERE s IN (TRUE, 1, 'abcd');]])
        t.assert_equals(res.rows, {{true}})

        --
        -- Make sure that BOOLEAN is not implicitly converted to INTEGER
        -- while inserted to PRIMARY KEY field.
        --
        assert_type_mismatch([[INSERT INTO t2 VALUES (true, 12345);]])

        execute([[DROP TABLE t2;]])

        -- Check that we can create index on field of type BOOLEAN.
        res = execute([[CREATE INDEX i0 ON t1(a);]])
        t.assert_equals(res.row_count, 1)

        -- Check boolean as LIMIT argument.
        local exp_err = {
            message = "Failed to execute SQL statement: "..
                      "Only positive integers are allowed in the LIMIT "..
                      "clause",
        }
        sql = [[SELECT * FROM t LIMIT TRUE;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT * FROM t LIMIT FALSE;]]
        t.assert_error_covers(exp_err, execute, sql)

        -- Check boolean as OFFSET argument.
        exp_err = {
            message = "Failed to execute SQL statement: "..
                      "Only positive integers are allowed in the OFFSET "..
                      "clause",
        }
        sql = [[SELECT * FROM t LIMIT 1 OFFSET TRUE;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT * FROM t LIMIT 1 OFFSET FALSE;]]
        t.assert_error_covers(exp_err, execute, sql)

        -- Check involvance in index search.
        exp = {
            {
                0, 0, 0,
                "SEARCH TABLE t1 USING COVERING INDEX i0 (a=?) (~10 rows)"
            }
        }
        sql = [[EXPLAIN QUERY PLAN SELECT a FROM t1 WHERE a = TRUE;]]
        res = execute(sql)
        t.assert_equals(res.rows, exp)

        local result = execute('EXPLAIN SELECT * FROM (VALUES(TRUE)), t;')
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

        res = execute([[SELECT is_boolean('TRUE');]])
        t.assert_equals(res.rows, {{false}})

        -- Check BOOLEAN as argument of scalar function.
        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function ABS()',
        }
        sql = [[SELECT ABS(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function LOWER()',
        }
        sql = [[SELECT LOWER(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function UPPER()',
        }
        sql = [[SELECT UPPER(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp = {
            {'FALSE'},
            {'TRUE'},
            {'NULL'},
            {'NULL'},
        }
        res = execute([[SELECT QUOTE(a) FROM t1;]])
        t.assert_equals(res.rows, exp)

        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function LENGTH()',
        }
        sql = [[SELECT LENGTH(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp = {
            {'boolean'},
            {'boolean'},
            {'NULL'},
            {'NULL'},
        }
        res = execute([[SELECT TYPEOF(a) FROM t1;]])
        t.assert_equals(res.rows, exp)

        -- Check BOOLEAN as argument of aggregate function.
        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function AVG()',
        }
        sql = [[SELECT AVG(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        res = execute([[SELECT MIN(a) FROM t1;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT MAX(a) FROM t1;]])
        t.assert_equals(res.rows, {{true}})

        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function SUM()',
        }
        sql = [[SELECT SUM(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        res = execute([[SELECT COUNT(a) FROM t1;]])
        t.assert_equals(res.rows, {{2}})

        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function TOTAL()'
        }
        sql = [[SELECT TOTAL(a) FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Failed to execute SQL statement: '..
                      'wrong arguments for function GROUP_CONCAT()'
        }
        sql = [[SELECT GROUP_CONCAT(a, ' +++ ') FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

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
        execute([[DROP TABLE t1;]])

        -- Check interactions with CHECK constraint.
        sql = [[CREATE TABLE t1 (i INT PRIMARY KEY, a BOOLEAN,
                CONSTRAINT ck CHECK(a != TRUE));]]
        execute(sql)
        execute([[INSERT INTO t1 VALUES (1, FALSE);]])

        exp_err = {
            message = "Check constraint 'ck' failed for a tuple"
        }
        sql = [[INSERT INTO t1 VALUES (2, TRUE);]]
        t.assert_error_covers(exp_err, execute, sql)
        execute([[DROP TABLE t1;]])

        -- Check interactions with FOREIGN KEY constraint.
        execute([[CREATE TABLE t1 (a BOOLEAN PRIMARY KEY,
                  b BOOLEAN REFERENCES t1(a));]])

        exp_err = {
            message = "Foreign key constraint 'fk_unnamed_t1_b_1' "..
                      "failed for field '2 (b)': foreign "..
                      "tuple was not found"
        }
        sql = [[INSERT INTO t1 VALUES (FALSE, TRUE)]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[INSERT INTO t1 VALUES (TRUE, FALSE)]]
        t.assert_error_covers(exp_err, execute, sql)

        local _, err = box.execute([[INSERT INTO t1 VALUES (TRUE, NULL)]])
        t.assert_equals(err, nil)

        _, err = box.execute([[INSERT INTO t1 VALUES (FALSE, TRUE)]])
        t.assert_equals(err, nil)
        execute([[DROP TABLE t1;]])

        -- Check interactions with UNIQUE constraint.
        execute([[CREATE TABLE t1 (i INT PRIMARY KEY, a BOOLEAN,
                  CONSTRAINT uq UNIQUE(a));]])
        execute([[INSERT INTO t1 VALUES (1, TRUE)]])
        execute([[INSERT INTO t1 VALUES (2, FALSE)]])

        exp_err = {
            message = 'Duplicate key exists in unique index '..
                      '"uq" in space "t1" with old tuple - [1, true] '..
                      'and new tuple - [3, true]'
        }
        sql = [[INSERT INTO t1 VALUES (3, TRUE)]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Duplicate key exists '..
                      'in unique index "uq" in space "t1" '..
                      'with old tuple - [2, false] '..
                      'and new tuple - [4, false]'
        }
        sql = [[INSERT INTO t1 VALUES (4, FALSE)]]
        t.assert_error_covers(exp_err, execute, sql)
        execute([[DROP TABLE t1;]])

        -- Check CAST from BOOLEAN to the other types.
        assert_type_mismatch([[SELECT CAST(TRUE AS INTEGER);]])

        assert_type_mismatch([[SELECT CAST(FALSE AS INTEGER);]])
        assert_type_mismatch([[SELECT CAST(TRUE AS NUMBER);]])
        assert_type_mismatch([[SELECT CAST(FALSE AS NUMBER);]])

        sql = [[SELECT CAST(TRUE AS TEXT), CAST(FALSE AS TEXT);]]
        res = execute(sql)
        t.assert_equals(res.rows, {{'TRUE', 'FALSE'}})

        sql = [[SELECT CAST(TRUE AS BOOLEAN), CAST(FALSE AS BOOLEAN);]]
        res = execute(sql)
        t.assert_equals(res.rows, {{true, false}})

        -- Check CAST to BOOLEAN from the other types.
        assert_type_mismatch([[SELECT CAST(100 AS BOOLEAN);]])
        assert_type_mismatch([[SELECT CAST(1 AS BOOLEAN);]])
        assert_type_mismatch([[SELECT CAST(0 AS BOOLEAN);]])
        assert_type_mismatch([[SELECT CAST(0.123 AS BOOLEAN);]])
        assert_type_mismatch([[SELECT CAST(0.0 AS BOOLEAN);]])

        res = execute([[SELECT CAST('TRUE' AS BOOLEAN),
                        CAST('false' AS BOOLEAN);]])
        t.assert_equals(res.rows, {{true, false}})

        res = execute([[SELECT CAST('TRUE' AS BOOLEAN),
                        CAST('FALSE' AS BOOLEAN);]])
        t.assert_equals(res.rows, {{true, false}})

        -- Check usage in trigger.
        execute([[CREATE TABLE t1 (i1 INT PRIMARY KEY, a BOOLEAN);]])
        execute([[CREATE TABLE t2 (i2 INT PRIMARY KEY AUTOINCREMENT,
                  b BOOLEAN);]])
        execute([[CREATE TRIGGER r AFTER INSERT ON t1 FOR EACH ROW BEGIN
                  INSERT INTO t2(b) VALUES(TRUE); END;]])
        execute([[INSERT INTO t1 VALUES (100, FALSE);]])
        execute([[DROP TRIGGER r;]])

        res = execute([[SELECT * FROM t1;]])
        t.assert_equals(res.rows, {{100, false}})

        res = execute([[SELECT * FROM t2;]])
        t.assert_equals(res.rows, {{1, true}})

        -- Check UNION, UNION ALL AND INTERSECT.
        execute([[INSERT INTO t2 VALUES (100, FALSE);]])

        exp = {
            {1, true},
            {100, false},
        }
        res = execute([[SELECT * FROM t1 UNION SELECT * FROM t2;]])
        t.assert_equals(res.rows, exp)

        exp ={
            {100, false},
            {1, true},
            {100, false},
        }
        res = execute([[SELECT * FROM t1 UNION ALL SELECT * FROM t2;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT * FROM t1 INTERSECT SELECT * FROM t2;]])
        t.assert_equals(res.rows, {{100, false}})

        -- Check SUBSELECT.
        execute([[INSERT INTO t2(b) SELECT a FROM t1;]])

        exp ={
            {1, true},
            {100, false},
            {101, false},
        }
        res = execute([[SELECT * FROM t2;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {100, 1, false, true},
            {100, 100, false, false},
            {100, 101, false, false},
        }
        res, err = execute([[SELECT * FROM
                             (SELECT t1.i1, t2.i2, a, b
                             FROM t1, t2 WHERE a = FALSE OR b = TRUE);]])
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, exp)

        -- Check VIEW.
        execute([[CREATE VIEW v AS SELECT b FROM t2;]])

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
        execute([[WITH temp(x, y) AS (VALUES (111, FALSE)
                  UNION ALL VALUES (222, TRUE))
                  INSERT INTO t1 SELECT * from temp;]])
        exp = {
            {100, false},
            {111, false},
            {222, true},
        }
        res = execute([[SELECT * FROM t1;]])
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
        res = execute([[WITH RECURSIVE cnt(x, y) AS (VALUES(1, FALSE)
                        UNION ALL SELECT x + 1, x % 2 == 1 FROM
                        cnt WHERE x < 10) SELECT x, y FROM cnt;]])
        t.assert_equals(res.rows, exp)

        -- Check JOINs.
        exp = {
            {100, false, 100, false},
            {100, false, 101, false},
            {111, false, 100, false},
            {111, false, 101, false},
            {222, true, 1, true},
        }
        res = execute([[SELECT * FROM t1 JOIN t2 ON t1.a = t2.b;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT * FROM t1 LEFT JOIN t2 ON t1.a = t2.b;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT * FROM t1 INNER JOIN t2 ON t1.a = t2.b;]])
        t.assert_equals(res.rows, exp)

        -- Check UPDATE.
        execute([[UPDATE t1 SET a = NOT a;]])

        exp = {
            {100, true},
            {111, true},
            {222, false},
        }
        res = execute([[SELECT * FROM t1;]])
        t.assert_equals(res.rows, exp)

        -- Check SWITCH-CASE.
        exp = {
            {100, true},
            {111, false},
            {222, false},
        }
        res = execute([[SELECT i1, CASE WHEN a == TRUE AND
                        i1 % 2 == 1 THEN FALSE
                        WHEN a == TRUE and i1 % 2 == 0 THEN TRUE
                        WHEN a != TRUE then FALSE
                        END AS a0 FROM t1;]])
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
        res = execute([[SELECT * FROM t1 UNION SELECT * FROM t2
                        ORDER BY a, i1;]])
        t.assert_equals(res.rows, exp)

        -- Check GROUP BY.
        exp = {
            {false, 3},
            {true, 3},
        }
        res = execute([[SELECT a, COUNT(*) FROM (SELECT * FROM t1
                        UNION SELECT * FROM t2) GROUP BY a;]])
        t.assert_equals(res.rows, exp)

        execute([[DROP VIEW v;]])
        execute([[DROP TABLE t1;]])
        execute([[DROP TABLE t2;]])

        box.schema.func.drop('check_t1_ck')
        box.schema.func.drop('return_type')
        box.schema.func.drop('is_boolean')
    end)
end

g.test_logical_operations = function(cg)
    cg.server:exec(function()
        -- Check interaction of BOOLEAN and INTEGER.
        local execute = _G.execute
        local assert_type_mismatch = _G.assert_type_mismatch

        execute([[CREATE TABLE t1 (a1 BOOLEAN PRIMARY KEY, a2 BOOLEAN);]])
        execute([[INSERT INTO t1 VALUES (true, false), (false, true);]])

        assert_type_mismatch([[SELECT TRUE AND 2;]])
        assert_type_mismatch([[SELECT TRUE OR 2;]])
        assert_type_mismatch([[SELECT FALSE OR 2;]])
        assert_type_mismatch([[SELECT 2 AND TRUE;]])

        local res = execute([[SELECT NOT TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT NOT FALSE;]])
        t.assert_equals(res.rows, {{true}})

        local exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, NOT a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT TRUE AND TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE AND TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE OR TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE OR FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE OR TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE OR FALSE;]])
        t.assert_equals(res.rows, {{false}})

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, TRUE AND a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, FALSE AND a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, TRUE OR a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, FALSE OR a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a AND TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, a AND FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, a OR TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, a OR FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, false},
            {true, false, false},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a AND a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, true},
            {true, false, true},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a OR a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        assert_type_mismatch([[SELECT TRUE AND 2;]])
        assert_type_mismatch([[SELECT TRUE OR 2;]])
        assert_type_mismatch([[SELECT FALSE OR 2;]])
        assert_type_mismatch([[SELECT 2 AND TRUE;]])

        res = execute([[SELECT FALSE AND 2;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT 2 AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 2 OR TRUE;]])
        assert_type_mismatch([[SELECT 2 OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 2 FROM t1]])
        assert_type_mismatch([[SELECT a1, a1 OR 2 FROM t1]])
        assert_type_mismatch([[SELECT a1, 2 AND a1 FROM t1]])
        assert_type_mismatch([[SELECT a1, 2 OR a1 FROM t1]])
        assert_type_mismatch([[SELECT a2, a2 AND 2 FROM t1]])
        assert_type_mismatch([[SELECT a2, a2 OR 2 FROM t1]])
        assert_type_mismatch([[SELECT a2, 2 AND a2 FROM t1]])
        assert_type_mismatch([[SELECT a2, 2 OR a2 FROM t1]])

        execute([[CREATE TABLE t2 (b INT PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES (123);]])
        assert_type_mismatch([[SELECT b, TRUE AND b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE OR b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE OR b FROM t2;]])
        assert_type_mismatch([[SELECT b, b AND TRUE FROM t2;]])

        res = execute([[SELECT b, FALSE AND b FROM t2;]])
        t.assert_equals(res.rows, {{123, false}})

        res = execute([[SELECT b, b AND FALSE FROM t2;]])
        t.assert_equals(res.rows, {{123, false}})

        assert_type_mismatch([[SELECT b, b OR TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b OR FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 AND b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 OR b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b AND a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b OR a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 AND b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 OR b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b AND a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b OR a2 FROM t1, t2;]])

        execute([[DROP TABLE t2;]])

        assert_type_mismatch([[SELECT TRUE AND 2.3;]])

        res = execute([[SELECT FALSE AND 2.3;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT TRUE OR 2.3;]])
        assert_type_mismatch([[SELECT FALSE OR 2.3;]])
        assert_type_mismatch([[SELECT 2.3 AND TRUE;]])

        res = execute([[SELECT 2.3 AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 2.3 OR TRUE;]])
        assert_type_mismatch([[SELECT 2.3 OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 OR 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 AND a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 OR a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 AND 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 OR 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 AND a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 OR a2 FROM t1;]])

        execute([[CREATE TABLE t2 (c DOUBLE PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES (4.56);]])

        assert_type_mismatch([[SELECT c, TRUE AND c FROM t2;]])

        res = execute([[SELECT c, FALSE AND c FROM t2;]])
        t.assert_equals(res.rows, {{4.56, false}})

        assert_type_mismatch([[SELECT c, TRUE OR c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE OR c FROM t2;]])
        assert_type_mismatch([[SELECT c, c AND TRUE FROM t2;]])

        res = execute([[SELECT c, c AND FALSE FROM t2;]])
        t.assert_equals(res.rows, {{4.56, false}})

        assert_type_mismatch([[SELECT c, c OR TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c OR FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 AND c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 OR c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c AND a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c OR a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 AND c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 OR c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c AND a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c OR a2 FROM t1, t2;]])

        execute([[DROP TABLE t2;]])

        assert_type_mismatch([[SELECT TRUE AND 'abc';]])

        res = execute([[SELECT FALSE AND 'abc';]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT TRUE OR 'abc';]])
        assert_type_mismatch([[SELECT FALSE OR 'abc';]])
        assert_type_mismatch([[SELECT 'abc' AND TRUE;]])

        res = execute([[SELECT 'abc' AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 'abc' OR TRUE;]])
        assert_type_mismatch([[SELECT 'abc' OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 'abc' FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 OR 'abc' FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'abc' AND a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'abc' OR a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 AND 'abc' FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 OR 'abc' FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'abc' AND a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'abc' OR a2 FROM t1;]])

        execute([[CREATE TABLE t2 (d TEXT PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES ('AsdF');]])

        assert_type_mismatch([[SELECT d, TRUE AND d FROM t2;]])

        res = execute([[SELECT d, FALSE AND d FROM t2;]])
        t.assert_equals(res.rows, {{'AsdF', false}})

        assert_type_mismatch([[SELECT d, TRUE OR d FROM t2;]])
        assert_type_mismatch([[SELECT d, false OR d FROM t2;]])
        assert_type_mismatch([[SELECT d, d AND TRUE FROM t2;]])

        res = execute([[SELECT d, d AND FALSE FROM t2;]])
        t.assert_equals(res.rows, {{'AsdF', false}})

        assert_type_mismatch([[SELECT d, d OR TRUE FROM t2;]])
        assert_type_mismatch([[SELECT d, d OR FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, d, a1 AND d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, d, a1 OR d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, d, d AND a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, d, d OR a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, a2 AND d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, a2 OR d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, d AND a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, d OR a2 FROM t1, t2;]])

        execute([[INSERT INTO t2 VALUES
                  ('TRUE'), ('true'), ('FALSE'), ('false');]])
        assert_type_mismatch([[SELECT TRUE AND 'TRUE';]])

        res = execute([[SELECT FALSE AND 'TRUE';]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT TRUE OR 'TRUE';]])
        assert_type_mismatch([[SELECT FALSE OR 'TRUE';]])
        assert_type_mismatch([[SELECT 'TRUE' AND TRUE;]])

        res = execute([[SELECT 'TRUE' AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 'TRUE' OR TRUE;]])
        assert_type_mismatch([[SELECT 'TRUE' OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 'TRUE' FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 OR 'TRUE' FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'TRUE' AND a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'TRUE' OR a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 AND 'TRUE' FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 OR 'TRUE' FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'TRUE' AND a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'TRUE' OR a2 FROM t1;]])

        assert_type_mismatch([[SELECT d, TRUE AND
                               d FROM t2 WHERE d = 'TRUE';]])

        res = execute([[SELECT d, FALSE AND d FROM t2 WHERE d = 'TRUE';]])
        t.assert_equals(res.rows, {{'TRUE', false}})

        assert_type_mismatch([[SELECT d, TRUE OR d FROM t2 WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT d, FALSE OR d FROM t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT d, d AND TRUE FROM
                               t2 WHERE d = 'TRUE';]])

        res = execute([[SELECT d, d AND FALSE FROM t2 WHERE d = 'TRUE';]])
        t.assert_equals(res.rows, {{'TRUE', false}})

        assert_type_mismatch([[SELECT d, d OR TRUE FROM t2 WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT d, d OR FALSE FROM t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a1, d, a1 AND d FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a1, d, a1 OR d FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a1, d, d AND a1 FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a1, d, d OR a1 FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a2, d, a2 AND d FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a2, d, a2 OR d FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a2, d, d AND a2 FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT a2, d, d OR a2 FROM t1, t2
                               WHERE d = 'TRUE';]])
        assert_type_mismatch([[SELECT TRUE AND 'true';]])

        res = execute([[SELECT FALSE AND 'true';]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT TRUE OR 'true';]])
        assert_type_mismatch([[SELECT FALSE OR 'true';]])
        assert_type_mismatch([[SELECT 'true' AND TRUE;]])

        res = execute([[SELECT 'true' AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 'true' OR TRUE;]])
        assert_type_mismatch([[SELECT 'true' OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 'true' FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 OR 'true' FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'true' AND a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'true' OR a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 AND 'true' FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 OR 'true' FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'true' AND a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'true' OR a2 FROM t1;]])
        assert_type_mismatch([[SELECT d, TRUE AND d FROM t2
                               WHERE d = 'true';]])

        res = execute([[SELECT d, FALSE AND d FROM t2 WHERE d = 'true';]])
        t.assert_equals(res.rows, {{'true', false}})

        assert_type_mismatch([[SELECT d, TRUE OR d FROM t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT d, FALSE OR d FROM t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT d, d AND TRUE FROM t2
                               WHERE d = 'true';]])

        res = execute([[SELECT d, d AND FALSE FROM t2
                        WHERE d = 'true';]])
        t.assert_equals(res.rows, {{'true', false}})

        assert_type_mismatch([[SELECT d, d OR TRUE FROM t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT d, d OR FALSE FROM t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a1, d, a1 AND d FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a1, d, a1 OR d FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a1, d, d AND a1 FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a1, d, d OR a1 FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a2, d, a2 AND d FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a2, d, a2 OR d FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a2, d, d AND a2 FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT a2, d, d OR a2 FROM t1, t2
                               WHERE d = 'true';]])
        assert_type_mismatch([[SELECT TRUE AND 'FALSE';]])

        res = execute([[SELECT FALSE AND 'FALSE';]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT TRUE OR 'FALSE';]])
        assert_type_mismatch([[SELECT FALSE OR 'FALSE';]])
        assert_type_mismatch([[SELECT 'FALSE' AND TRUE;]])

        res = execute([[SELECT 'FALSE' AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 'FALSE' OR TRUE;]])
        assert_type_mismatch([[SELECT 'FALSE' OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 'FALSE' FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 OR 'FALSE' FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'FALSE' AND a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'FALSE' OR a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 AND 'FALSE' FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 OR 'FALSE' FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'FALSE' AND a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'FALSE' OR a2 FROM t1;]])
        assert_type_mismatch([[SELECT d, TRUE AND d FROM t2
                               WHERE d = 'FALSE';]])

        res = execute([[SELECT d, FALSE AND d FROM t2 WHERE d = 'FALSE';]])
        t.assert_equals(res.rows, {{'FALSE', false}})

        assert_type_mismatch([[SELECT d, TRUE OR d FROM t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT d, FALSE OR d FROM t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT d, d AND TRUE FROM t2
                               WHERE d = 'FALSE';]])

        res = execute([[SELECT d, d AND FALSE FROM t2
                        WHERE d = 'FALSE';]])
        t.assert_equals(res.rows, {{'FALSE', false}})

        assert_type_mismatch([[SELECT d, d OR TRUE FROM t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT d, d OR FALSE FROM t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a1, d, a1 AND d FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a1, d, a1 OR d FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a1, d, d AND a1 FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a1, d, d OR a1 FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a2, d, a2 AND d FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a2, d, a2 OR d FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a2, d, d AND a2 FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT a2, d, d OR a2 FROM t1, t2
                               WHERE d = 'FALSE';]])
        assert_type_mismatch([[SELECT TRUE AND 'false';]])

        res = execute([[SELECT FALSE AND 'false';]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT TRUE OR 'false';]])
        assert_type_mismatch([[SELECT FALSE OR 'false';]])
        assert_type_mismatch([[SELECT 'false' AND TRUE;]])

        res = execute([[SELECT 'false' AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        assert_type_mismatch([[SELECT 'false' OR TRUE;]])
        assert_type_mismatch([[SELECT 'false' OR FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 AND 'false' FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 OR 'false' FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'false' AND a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 'false' OR a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 AND 'false' FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 OR 'false' FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'false' AND a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 'false' OR a2 FROM t1;]])
        assert_type_mismatch([[SELECT d, TRUE AND d FROM t2
                               WHERE d = 'false';]])

        res = execute([[SELECT d, FALSE AND d FROM t2
                        WHERE d = 'false';]])
        t.assert_equals(res.rows, {{'false', false}})

        assert_type_mismatch([[SELECT d, TRUE OR d FROM t2
                               WHERE d = 'false';]])

        assert_type_mismatch([[SELECT d, FALSE OR d FROM t2
                               WHERE d = 'false';]])

        assert_type_mismatch([[SELECT d, d AND TRUE FROM t2
                               WHERE d = 'false';]])

        res = execute([[SELECT d, d AND FALSE FROM t2
                        WHERE d = 'false';]])
        t.assert_equals(res.rows, {{'false', false}})

        assert_type_mismatch([[SELECT d, d OR TRUE FROM t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT d, d OR FALSE FROM t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a1, d, a1 AND d FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a1, d, a1 OR d FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a1, d, d AND a1 FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a1, d, d OR a1 FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a2, d, a2 AND d FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a2, d, a2 OR d FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a2, d, d AND a2 FROM t1, t2
                               WHERE d = 'false';]])
        assert_type_mismatch([[SELECT a2, d, d OR a2 FROM t1, t2
                               WHERE d = 'false';]])
        execute([[DROP TABLE t1;]])
        execute([[DROP TABLE t2;]])
    end)
end

g.test_arithmetic_operations = function(cg)
    cg.server:exec(function()
        local execute = _G.execute
        local assert_type_mismatch = _G.assert_type_mismatch

        assert_type_mismatch([[SELECT TRUE * TRUE;]])
        assert_type_mismatch([[SELECT TRUE * FALSE;]])
        assert_type_mismatch([[SELECT TRUE / TRUE;]])
        assert_type_mismatch([[SELECT TRUE / FALSE;]])
        assert_type_mismatch([[SELECT -FALSE;]])
        assert_type_mismatch([[SELECT -TRUE;]])
        assert_type_mismatch([[SELECT -a FROM t;]])
        assert_type_mismatch([[SELECT FALSE * TRUE;]])
        assert_type_mismatch([[SELECT FALSE * FALSE;]])
        assert_type_mismatch([[SELECT FALSE / TRUE;]])
        assert_type_mismatch([[SELECT FALSE / FALSE;]])
        assert_type_mismatch([[SELECT TRUE + TRUE;]])
        assert_type_mismatch([[SELECT TRUE + FALSE;]])
        assert_type_mismatch([[SELECT TRUE - TRUE;]])
        assert_type_mismatch([[SELECT TRUE - FALSE;]])
        assert_type_mismatch([[SELECT FALSE + TRUE;]])
        assert_type_mismatch([[SELECT FALSE + FALSE;]])
        assert_type_mismatch([[SELECT FALSE - TRUE;]])
        assert_type_mismatch([[SELECT FALSE - FALSE;]])
        assert_type_mismatch([[SELECT TRUE % TRUE;]])
        assert_type_mismatch([[SELECT TRUE % FALSE;]])
        assert_type_mismatch([[SELECT FALSE % TRUE;]])
        assert_type_mismatch([[SELECT FALSE % FALSE;]])
        assert_type_mismatch([[SELECT a, TRUE + a FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE - a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE + a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE - a FROM t;]])
        assert_type_mismatch([[SELECT a, a + TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a + FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, a - TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a - FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE * a FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE / a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE * a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE / a FROM t;]])
        assert_type_mismatch([[SELECT a, a * TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a * FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, a / TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a / FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE % a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE % a FROM t;]])
        assert_type_mismatch([[SELECT a, a % TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a % FALSE FROM t;]])

        execute([[CREATE TABLE t1 (a1 BOOLEAN PRIMARY KEY, a2 BOOLEAN);]])
        execute([[INSERT INTO t1 VALUES (true, false), (false, true);]])

        assert_type_mismatch([[SELECT a, a1, a + a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a1, a - a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a1, a * a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a1, a / a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a1, a % a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT ~TRUE;]])
        assert_type_mismatch([[SELECT TRUE & TRUE;]])
        assert_type_mismatch([[SELECT FALSE & TRUE;]])
        assert_type_mismatch([[SELECT TRUE | TRUE;]])
        assert_type_mismatch([[SELECT FALSE | TRUE;]])
        assert_type_mismatch([[SELECT TRUE << TRUE;]])
        assert_type_mismatch([[SELECT FALSE << TRUE;]])
        assert_type_mismatch([[SELECT TRUE >> TRUE;]])
        assert_type_mismatch([[SELECT FALSE >> TRUE;]])
        assert_type_mismatch([[SELECT ~FALSE;]])
        assert_type_mismatch([[SELECT FALSE & FALSE;]])
        assert_type_mismatch([[SELECT TRUE & FALSE;]])
        assert_type_mismatch([[SELECT FALSE | FALSE;]])
        assert_type_mismatch([[SELECT TRUE | FALSE;]])
        assert_type_mismatch([[SELECT FALSE << FALSE;]])
        assert_type_mismatch([[SELECT TRUE << FALSE;]])
        assert_type_mismatch([[SELECT TRUE >> FALSE;]])
        assert_type_mismatch([[SELECT FALSE >> FALSE;]])
        assert_type_mismatch([[SELECT a, TRUE & a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE & a FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE | a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE | a FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE << a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE << a FROM t;]])
        assert_type_mismatch([[SELECT a, TRUE >> a FROM t;]])
        assert_type_mismatch([[SELECT a, FALSE >> a FROM t;]])
        assert_type_mismatch([[SELECT a, a >> TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a & TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a | TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a << TRUE FROM t;]])
        assert_type_mismatch([[SELECT a, a >> FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, a << FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, a | FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, a1, a & a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a & FALSE FROM t;]])
        assert_type_mismatch([[SELECT a, a1, a | a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a1, a << a1 FROM t, t1;]])
        assert_type_mismatch([[SELECT a, a1, a >> a1 FROM t, t1;]])

        -- Check concatenate.
        local exp_err = {
            message = 'Inconsistent types: expected string or '..
                      'varbinary got boolean(TRUE)'
        }
        local sql = [[SELECT TRUE || TRUE;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT FALSE || TRUE;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a, a || TRUE FROM t;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a2, a2 || a2 FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a2, a2 || a2 FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a1, a2, a2 || a2 FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Inconsistent types: expected string or '..
                      'varbinary got boolean(FALSE)'
        }
        sql = [[SELECT TRUE || FALSE;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT FALSE || FALSE;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a, TRUE || a FROM t;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a, FALSE || a FROM t;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a, a || FALSE FROM t;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a1, a1 || a1 FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT a1, a2, a1 || a1 FROM t1;]]
        t.assert_error_covers(exp_err, execute, sql)

        -- Check comparisons.
        local res = execute([[SELECT TRUE > TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE > FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE > TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE > FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE < TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE < FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE < TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE < FALSE;]])
        t.assert_equals(res.rows, {{false}})

        local exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, TRUE > a FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, FALSE > a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, TRUE < a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a > TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a < FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, FALSE < a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a > FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, a < TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, false},
            {true, false, true},
            {true, true, false},
        }
        res = execute([[SELECT a, a1, a > a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, true},
            {true, false, false},
            {true, true, false},
        }
        res = execute([[SELECT a, a1, a < a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT TRUE >= TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE >= FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE >= TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE >= FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE <= TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE <= FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE <= TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE <= FALSE;]])
        t.assert_equals(res.rows, {{true}})

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, TRUE >= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, FALSE <= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a >= FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a <= TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, FALSE >= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a <= FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, TRUE <= a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a >= TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, true},
            {false, true, false},
            {true, false, true},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a >= a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, true},
            {false, true, true},
            {true, false, false},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a <= a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT TRUE == TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE == FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE == TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE == FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE != TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE != FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE != TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE != FALSE;]])
        t.assert_equals(res.rows, {{false}})

        exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, TRUE == a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, FALSE != a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a == TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a != FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, FALSE == a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, TRUE != a FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a == FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a != TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, true},
            {false, true, false},
            {true, false, false},
            {true, true, true},
        }
        res = execute([[SELECT a, a1, a == a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false, false},
            {false, true, true},
            {true, false, true},
            {true, true, false},
        }
        res = execute([[SELECT a, a1, a != a1 FROM t, t1;]])
        t.assert_equals(res.rows, exp)

        assert_type_mismatch([[SELECT TRUE + 2;]])
        assert_type_mismatch([[SELECT TRUE - 2;]])
        assert_type_mismatch([[SELECT FALSE + 2;]])
        assert_type_mismatch([[SELECT FALSE - 2;]])
        assert_type_mismatch([[SELECT TRUE * 2;]])
        assert_type_mismatch([[SELECT TRUE / 2;]])
        assert_type_mismatch([[SELECT 2 + TRUE;]])
        assert_type_mismatch([[SELECT 2 - TRUE;]])
        assert_type_mismatch([[SELECT 2 * TRUE;]])
        assert_type_mismatch([[SELECT 2 / TRUE;]])
        assert_type_mismatch([[SELECT FALSE * 2;]])
        assert_type_mismatch([[SELECT FALSE / 2;]])
        assert_type_mismatch([[SELECT 2 + FALSE;]])
        assert_type_mismatch([[SELECT 2 - FALSE;]])
        assert_type_mismatch([[SELECT 2 * FALSE;]])
        assert_type_mismatch([[SELECT 2 / FALSE;]])
        assert_type_mismatch([[SELECT TRUE % 2;]])
        assert_type_mismatch([[SELECT 2 % TRUE;]])
        assert_type_mismatch([[SELECT FALSE % 2;]])
        assert_type_mismatch([[SELECT 2 % FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 + 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 - 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 * 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 / 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 + a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 - a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 * a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 / a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 % 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 % a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 + 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 - 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 * 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 / 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 + a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 - a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 * a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 / a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 % 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 % a2 FROM t1;]])

        execute([[CREATE TABLE t2 (b INT PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES (123);]])

        assert_type_mismatch([[SELECT b, TRUE + b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE - b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE + b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE - b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE * b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE / b FROM t2;]])
        assert_type_mismatch([[SELECT b, b + TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b - TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b * TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b / TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE * b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE / b FROM t2;]])
        assert_type_mismatch([[SELECT b, b + FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b - FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b * FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b / FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE % b FROM t2;]])
        assert_type_mismatch([[SELECT b, b % TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE % b FROM t2;]])
        assert_type_mismatch([[SELECT b, b % FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 + b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 - b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 * b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 / b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b + a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b - a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b * a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b / a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 % b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b % a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 + b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 - b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 * b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 / b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b + a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b - a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b * a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b / a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 % b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b % a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE & 2;]])
        assert_type_mismatch([[SELECT TRUE | 2;]])
        assert_type_mismatch([[SELECT TRUE << 2;]])
        assert_type_mismatch([[SELECT TRUE >> 2;]])
        assert_type_mismatch([[SELECT 2 & TRUE;]])
        assert_type_mismatch([[SELECT 2 | TRUE;]])
        assert_type_mismatch([[SELECT 2 << TRUE;]])
        assert_type_mismatch([[SELECT 2 >> TRUE;]])
        assert_type_mismatch([[SELECT FALSE & 2;]])
        assert_type_mismatch([[SELECT FALSE | 2;]])
        assert_type_mismatch([[SELECT FALSE << 2;]])
        assert_type_mismatch([[SELECT FALSE >> 2;]])
        assert_type_mismatch([[SELECT 2 & FALSE;]])
        assert_type_mismatch([[SELECT 2 | FALSE;]])
        assert_type_mismatch([[SELECT 2 << FALSE;]])
        assert_type_mismatch([[SELECT 2 >> FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 & 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 | 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 << 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 >> 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 & a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 | a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 << a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 >> a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 & 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 | 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 << 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 >> 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 & a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 | a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 << a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 >> a2 FROM t1;]])
        assert_type_mismatch([[SELECT b, TRUE & b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE | b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE << b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE >> b FROM t2;]])
        assert_type_mismatch([[SELECT b, b & TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b | TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b << TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b >> TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE & b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE | b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE << b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE >> b FROM t2;]])
        assert_type_mismatch([[SELECT b, b & FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b | FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b << FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b >> FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 & b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 | b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 << b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 >> b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b & a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b | a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b << a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b >> a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 & b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 | b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 << b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 >> b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b & a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b | a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b << a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b >> a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE > 2;]])
        assert_type_mismatch([[SELECT FALSE > 2;]])
        assert_type_mismatch([[SELECT TRUE < 2;]])
        assert_type_mismatch([[SELECT FALSE < 2;]])
        assert_type_mismatch([[SELECT 2 > TRUE;]])
        assert_type_mismatch([[SELECT 2 < TRUE;]])
        assert_type_mismatch([[SELECT 2 > FALSE;]])
        assert_type_mismatch([[SELECT 2 < FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 > 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 < 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 > 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 < 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 > a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 < a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 > a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 < a2 FROM t1;]])
        assert_type_mismatch([[SELECT b, TRUE > b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE > b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE < b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE < b FROM t2;]])
        assert_type_mismatch([[SELECT b, b > TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b < TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b > FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b < FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 > b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 < b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 > b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 < b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b > a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b < a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b > a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b < a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE >= 2;]])
        assert_type_mismatch([[SELECT FALSE >= 2;]])
        assert_type_mismatch([[SELECT TRUE <= 2;]])
        assert_type_mismatch([[SELECT FALSE <= 2;]])
        assert_type_mismatch([[SELECT 2 >= TRUE;]])
        assert_type_mismatch([[SELECT 2 <= TRUE;]])
        assert_type_mismatch([[SELECT 2 >= FALSE;]])
        assert_type_mismatch([[SELECT 2 <= FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 >= 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 <= 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 >= 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 <= 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 >= a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 <= a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 >= a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 <= a2 FROM t1;]])
        assert_type_mismatch([[SELECT b, TRUE >= b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE >= b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE <= b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE <= b FROM t2;]])
        assert_type_mismatch([[SELECT b, b >= TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b <= TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b >= FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b <= FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 >= b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 <= b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 >= b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 <= b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b >= a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b <= a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b >= a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b <= a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE == 2;]])
        assert_type_mismatch([[SELECT FALSE == 2;]])
        assert_type_mismatch([[SELECT TRUE != 2;]])
        assert_type_mismatch([[SELECT FALSE != 2;]])
        assert_type_mismatch([[SELECT 2 == TRUE;]])
        assert_type_mismatch([[SELECT 2 != TRUE;]])
        assert_type_mismatch([[SELECT 2 == FALSE;]])
        assert_type_mismatch([[SELECT 2 != FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 == 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 != 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 == 2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 != 2 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 == a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2 != a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 == a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2 != a2 FROM t1;]])
        assert_type_mismatch([[SELECT b, TRUE == b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE == b FROM t2;]])
        assert_type_mismatch([[SELECT b, TRUE != b FROM t2;]])
        assert_type_mismatch([[SELECT b, FALSE != b FROM t2;]])
        assert_type_mismatch([[SELECT b, b == TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b != TRUE FROM t2;]])
        assert_type_mismatch([[SELECT b, b == FALSE FROM t2;]])
        assert_type_mismatch([[SELECT b, b != FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 == b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, a1 != b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 == b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, a2 != b FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b == a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, b, b != a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b == a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, b, b != a2 FROM t1, t2;]])

        execute([[DROP TABLE t2;]])

        assert_type_mismatch([[SELECT TRUE + 2.3;]])
        assert_type_mismatch([[SELECT TRUE - 2.3;]])
        assert_type_mismatch([[SELECT FALSE + 2.3;]])
        assert_type_mismatch([[SELECT FALSE - 2.3;]])
        assert_type_mismatch([[SELECT TRUE * 2.3;]])
        assert_type_mismatch([[SELECT TRUE / 2.3;]])
        assert_type_mismatch([[SELECT 2.3 + TRUE;]])
        assert_type_mismatch([[SELECT 2.3 - TRUE;]])
        assert_type_mismatch([[SELECT 2.3 * TRUE;]])
        assert_type_mismatch([[SELECT 2.3 / TRUE;]])

        assert_type_mismatch([[SELECT FALSE * 2.3;]])
        assert_type_mismatch([[SELECT FALSE / 2.3;]])
        assert_type_mismatch([[SELECT 2.3 + FALSE;]])
        assert_type_mismatch([[SELECT 2.3 - FALSE;]])
        assert_type_mismatch([[SELECT 2.3 * FALSE;]])
        assert_type_mismatch([[SELECT 2.3 / FALSE;]])
        assert_type_mismatch([[SELECT TRUE % 2.3;]])
        assert_type_mismatch([[SELECT 2.3 % TRUE;]])
        assert_type_mismatch([[SELECT FALSE % 2.3;]])
        assert_type_mismatch([[SELECT 2.3 % FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 + 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 - 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 * 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 / 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 + a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 - a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 * a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 / a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 % 2.3 FROM t1;]])

        assert_type_mismatch([[SELECT a1, 2.3 % a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 + 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 - 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 * 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 / 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 + a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 - a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 * a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 / a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 % 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 % a2 FROM t1;]])

        execute([[CREATE TABLE t2 (c DOUBLE PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES (4.56);]])

        assert_type_mismatch([[SELECT c, TRUE + c FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE - c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE + c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE - c FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE * c FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE / c FROM t2;]])
        assert_type_mismatch([[SELECT c, c + TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c - TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c * TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c / TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE * c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE / c FROM t2;]])
        assert_type_mismatch([[SELECT c, c + FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, c - FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, c * FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, c / FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE % c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE % c FROM t2;]])
        assert_type_mismatch([[SELECT c, c % TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c % FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 + c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 - c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 * c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 / c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c + a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c - a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c * a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c / a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 % c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c % a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 + c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 - c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 * c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 / c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c + a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c - a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c * a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c / a2 FROM t1, t2;]])

        assert_type_mismatch([[SELECT a2, c, a2 % c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c % a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE > 2.3;]])
        assert_type_mismatch([[SELECT FALSE > 2.3;]])
        assert_type_mismatch([[SELECT TRUE < 2.3;]])
        assert_type_mismatch([[SELECT FALSE < 2.3;]])
        assert_type_mismatch([[SELECT 2.3 > TRUE;]])
        assert_type_mismatch([[SELECT 2.3 < TRUE;]])
        assert_type_mismatch([[SELECT 2.3 > FALSE;]])
        assert_type_mismatch([[SELECT 2.3 < FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 > 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 < 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 > 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 < 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 > a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 < a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 > a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 < a2 FROM t1;]])
        assert_type_mismatch([[SELECT c, TRUE > c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE > c FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE < c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE < c FROM t2;]])

        assert_type_mismatch([[SELECT c, c > TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c < TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c > FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, c < FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 > c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 < c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 > c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 < c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c > a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c < a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c > a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c < a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE >= 2.3;]])
        assert_type_mismatch([[SELECT FALSE >= 2.3;]])
        assert_type_mismatch([[SELECT TRUE <= 2.3;]])
        assert_type_mismatch([[SELECT FALSE <= 2.3;]])
        assert_type_mismatch([[SELECT 2.3 >= TRUE;]])
        assert_type_mismatch([[SELECT 2.3 <= TRUE;]])
        assert_type_mismatch([[SELECT 2.3 >= FALSE;]])
        assert_type_mismatch([[SELECT 2.3 <= FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 >= 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 <= 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 >= 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 <= 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 >= a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 <= a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 >= a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 <= a2 FROM t1;]])

        assert_type_mismatch([[SELECT c, TRUE >= c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE >= c FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE <= c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE <= c FROM t2;]])
        assert_type_mismatch([[SELECT c, c >= TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c <= TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c >= FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, c <= FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 >= c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 <= c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 >= c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 <= c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c >= a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c <= a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c >= a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c <= a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT TRUE == 2.3;]])
        assert_type_mismatch([[SELECT FALSE == 2.3;]])
        assert_type_mismatch([[SELECT TRUE != 2.3;]])
        assert_type_mismatch([[SELECT FALSE != 2.3;]])
        assert_type_mismatch([[SELECT 2.3 == TRUE;]])
        assert_type_mismatch([[SELECT 2.3 != TRUE;]])
        assert_type_mismatch([[SELECT 2.3 == FALSE;]])
        assert_type_mismatch([[SELECT 2.3 != FALSE;]])
        assert_type_mismatch([[SELECT a1, a1 == 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, a1 != 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 == 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 != 2.3 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 == a1 FROM t1;]])
        assert_type_mismatch([[SELECT a1, 2.3 != a1 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 == a2 FROM t1;]])
        assert_type_mismatch([[SELECT a2, 2.3 != a2 FROM t1;]])
        assert_type_mismatch([[SELECT c, TRUE == c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE == c FROM t2;]])
        assert_type_mismatch([[SELECT c, TRUE != c FROM t2;]])
        assert_type_mismatch([[SELECT c, FALSE != c FROM t2;]])

        assert_type_mismatch([[SELECT c, c == TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c != TRUE FROM t2;]])
        assert_type_mismatch([[SELECT c, c == FALSE FROM t2;]])
        assert_type_mismatch([[SELECT c, c != FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 == c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, a1 != c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 == c FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, a2 != c FROM t1, t2;]])

        assert_type_mismatch([[SELECT a1, c, c == a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, c, c != a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c == a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, c, c != a2 FROM t1, t2;]])

        execute([[DROP TABLE t2;]])

        assert_type_mismatch([[SELECT TRUE > 'abc';]])
        assert_type_mismatch([[SELECT FALSE > 'abc';]])
        assert_type_mismatch([[SELECT TRUE < 'abc';]])
        assert_type_mismatch([[SELECT FALSE < 'abc';]])
        assert_type_mismatch([[SELECT 'abc' > TRUE;]])
        assert_type_mismatch([[SELECT 'abc' < TRUE;]])
        assert_type_mismatch([[SELECT 'abc' > FALSE;]])
        assert_type_mismatch([[SELECT 'abc' < FALSE;]])

        execute([[CREATE TABLE t2 (d TEXT PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES ('AsdF');]])

        assert_type_mismatch([[SELECT d, TRUE > d FROM t2;]])
        assert_type_mismatch([[SELECT d, FALSE > d FROM t2;]])
        assert_type_mismatch([[SELECT d, TRUE < d FROM t2;]])
        assert_type_mismatch([[SELECT d, FALSE < d FROM t2;]])
        assert_type_mismatch([[SELECT d, d > TRUE FROM t2;]])
        assert_type_mismatch([[SELECT d, d < TRUE FROM t2;]])
        assert_type_mismatch([[SELECT d, d > FALSE FROM t2;]])
        assert_type_mismatch([[SELECT d, d < FALSE FROM t2;]])
        assert_type_mismatch([[SELECT a1, d, a1 > d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, d, a1 < d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, a2 > d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, a2 < d FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, d, d > a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a1, d, d < a1 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, d > a2 FROM t1, t2;]])
        assert_type_mismatch([[SELECT a2, d, d < a2 FROM t1, t2;]])

        exp_err = {
            message = 'Inconsistent types: '..
                      'expected string or varbinary got boolean(TRUE)'
        }
        sql = [[SELECT TRUE || 'abc';]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT 'abc' || TRUE;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Inconsistent types: '..
                      'expected string or varbinary got boolean(FALSE)'
        }
        sql = [[SELECT FALSE || 'abc';]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT 'abc' || FALSE;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Inconsistent types: '..
                      'expected string or varbinary got boolean(TRUE)'
        }
        sql = [[SELECT d, TRUE || d FROM t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT d, d || TRUE FROM t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Inconsistent types: '..
                      'expected string or varbinary got boolean(FALSE)'
        }
        sql = [[SELECT d, FALSE || d FROM t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT d, d || FALSE FROM t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT d, a1 || d FROM t1, t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT d, d || a1 FROM t1, t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        exp_err = {
            message = 'Inconsistent types: '..
                      'expected string or varbinary got boolean(TRUE)'
        }
        sql = [[SELECT d, a2 || d FROM t1, t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        sql = [[SELECT d, d || a2 FROM t1, t2;]]
        t.assert_error_covers(exp_err, execute, sql)

        execute([[DROP TABLE t1;]])
        execute([[DROP TABLE t2;]])
    end)
end

g.test_in_operations = function(cg)
    cg.server:exec(function()
        local execute = _G.execute
        local assert_type_mismatch = _G.assert_type_mismatch

        local res = execute([[SELECT TRUE IN (TRUE);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE IN (TRUE);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE IN (FALSE);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE IN (FALSE);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE IN (TRUE, FALSE);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE IN (TRUE, FALSE);]])
        t.assert_equals(res.rows, {{true}})

        execute([[CREATE TABLE t1 (a1 BOOLEAN PRIMARY KEY, a2 BOOLEAN);]])
        execute([[INSERT INTO t1 VALUES (true, false), (false, true);]])

        res = execute([[SELECT TRUE IN (SELECT a1 FROM t1);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE IN (SELECT a1 FROM t1);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE IN (SELECT a1 FROM t1 LIMIT 1);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE IN (SELECT a1 FROM t1 LIMIT 1);]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE IN (1, 1.2, 'TRUE', FALSE);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE IN (1, 1.2, 'TRUE', FALSE);]])
        t.assert_equals(res.rows, {{true}})

        local exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, a IN (TRUE) FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, a IN (FALSE) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a IN (SELECT a1 FROM
                            t1 LIMIT 1) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a IN (1, 1.2, 'TRUE', FALSE) FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, a IN (TRUE, FALSE) FROM t;]])
        t.assert_equals(res.rows, exp)

        res = execute([[SELECT a, a IN (SELECT a1 FROM t1) FROM t;]])
        t.assert_equals(res.rows, exp)

        local sql = [[SELECT a1 IN (0.1, 1.2, 2.3, 3.4) FROM t1 LIMIT 1;]]
        res = execute(sql)
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT a2 IN (0.1, 1.2, 2.3, 3.4)
                        FROM t1 LIMIT 1;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE IN (0, 1, 2, 3);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE IN (0, 1, 2, 3);]])
        t.assert_equals(res.rows, {{false}})

        execute([[CREATE TABLE t2 (b INT PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES (123);]])

        assert_type_mismatch([[SELECT TRUE IN (SELECT b FROM t2);]])
        assert_type_mismatch([[SELECT FALSE IN (SELECT b FROM t2);]])

        execute([[DROP TABLE t2;]])

        res = execute([[SELECT TRUE IN (0.1, 1.2, 2.3, 3.4);]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE IN (0.1, 1.2, 2.3, 3.4);]])
        t.assert_equals(res.rows, {{false}})

        execute([[CREATE TABLE t2 (c DOUBLE PRIMARY KEY);]])
        execute([[INSERT INTO t2 VALUES (4.56);]])

        assert_type_mismatch([[SELECT TRUE IN (SELECT c FROM t2);]])

        assert_type_mismatch([[SELECT a2 IN (SELECT c FROM t2)
                               FROM t1 LIMIT 1;]])

        assert_type_mismatch([[SELECT FALSE IN (SELECT c FROM t2);]])

        assert_type_mismatch([[SELECT a1 IN (SELECT c FROM t2)
                               FROM t1 LIMIT 1;]])
        execute([[DROP TABLE t1;]])
        execute([[DROP TABLE t2;]])
    end)
end

g.test_between_operations = function(cg)
    cg.server:exec(function()
        local execute = _G.execute
        local assert_type_mismatch = _G.assert_type_mismatch

        local res = execute([[SELECT TRUE BETWEEN TRUE AND TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE BETWEEN TRUE AND TRUE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE BETWEEN FALSE AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE BETWEEN FALSE AND FALSE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT TRUE BETWEEN TRUE AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT FALSE BETWEEN TRUE AND FALSE;]])
        t.assert_equals(res.rows, {{false}})

        res = execute([[SELECT TRUE BETWEEN FALSE AND TRUE;]])
        t.assert_equals(res.rows, {{true}})

        res = execute([[SELECT FALSE BETWEEN FALSE AND TRUE;]])
        t.assert_equals(res.rows, {{true}})

        local exp = {
            {false, false},
            {true, true},
        }
        res = execute([[SELECT a, a BETWEEN TRUE AND TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, false},
        }
        res = execute([[SELECT a, a BETWEEN FALSE AND FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a, a BETWEEN TRUE AND FALSE FROM t;]])
        t.assert_equals(res.rows, exp)

        exp = {
            {false, true},
            {true, true},
        }
        res = execute([[SELECT a, a BETWEEN FALSE AND TRUE FROM t;]])
        t.assert_equals(res.rows, exp)

        execute([[CREATE TABLE t1 (a1 BOOLEAN PRIMARY KEY, a2 BOOLEAN);]])
        execute([[INSERT INTO t1 VALUES (true, false), (false, true);]])

        exp = {
            {false, false},
            {true, false},
        }
        res = execute([[SELECT a1, a1 IN (0, 1, 2, 3) FROM t1;]])
        t.assert_equals(res.rows, exp)

        assert_type_mismatch([[SELECT TRUE BETWEEN 0 AND 10;]])
        assert_type_mismatch([[SELECT FALSE BETWEEN 0 AND 10;]])
        assert_type_mismatch([[SELECT a1, a1 BETWEEN 0 AND 10 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 BETWEEN 0 AND 10 FROM t1;]])

        assert_type_mismatch([[SELECT TRUE BETWEEN 0.1 and 9.9;]])
        assert_type_mismatch([[SELECT FALSE BETWEEN 0.1 and 9.9;]])
        assert_type_mismatch([[SELECT a1, a1 BETWEEN 0.1 and 9.9 FROM t1;]])
        assert_type_mismatch([[SELECT a2, a2 BETWEEN 0.1 and 9.9 FROM t1;]])

        execute([[DROP TABLE t1;]])
    end)
end
