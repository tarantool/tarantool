local server = require('luatest.server')
local t = require('luatest')

local g = t.group("types", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
-- gh-3018: typeless columns are prohibited.
--
g.test_3018 = function(cg)
    cg.server:exec(function()
        local _, err = box.execute("CREATE TABLE t1 (id PRIMARY KEY);")
        local exp_err = "At line 1 at or near position 21: keyword 'PRIMARY'" ..
                        " is reserved. Please use double quotes if 'PRIMARY'" ..
                        " is an identifier."
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("CREATE TABLE t1 (a, id INT PRIMARY KEY);")
        t.assert_equals(err.message, "Syntax error at line 1 near ','")
        _, err = box.execute("CREATE TABLE t1 (id PRIMARY KEY, a INT);")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a);")
        t.assert_equals(err.message, "Syntax error at line 1 near ')'")
        _, err = box.execute([[CREATE TABLE t1 (id INT PRIMARY KEY, a INT,
                                                b UNIQUE);]])
        exp_err = "At line 2 at or near position 51: keyword 'UNIQUE' is " ..
                  "reserved. Please use double quotes if 'UNIQUE' is " ..
                  "an identifier."
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- gh-3104: real type is stored in space format.
--
g.test_3104 = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1 (id TEXT PRIMARY KEY, a NUMBER, b INT,
                                       c TEXT, d SCALAR);]])
        local res = box.space.t1:format()
        local exp = {
            {type = 'string', nullable_action = 'abort',
             name = 'id', is_nullable = false},
            {type = 'number', nullable_action = 'none',
             name = 'a', is_nullable = true},
            {type = 'integer', nullable_action = 'none',
             name = 'b', is_nullable = true},
            {type = 'string', nullable_action = 'none',
             name = 'c', is_nullable = true},
            {type = 'scalar', nullable_action = 'none',
             name = 'd', is_nullable = true},
        }
        t.assert_equals(res, exp)
        box.execute("CREATE VIEW v1 AS SELECT b + a, b - a FROM t1;")
        res = box.space.v1:format()
        exp = {
            {type = 'number', nullable_action = 'none',
             name = 'COLUMN_1', is_nullable = true},
            {type = 'number', nullable_action = 'none',
             name = 'COLUMN_2', is_nullable = true},
        }
        t.assert_equals(res, exp)
        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")
    end)
end

--
-- gh-2494: index's part also features correct declared type.
--
g.test_2494 = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1 (id TEXT PRIMARY KEY, a NUMBER, b INT,
                                       c TEXT, d SCALAR);]])
        box.execute("CREATE VIEW v1 AS SELECT b + a, b - a FROM t1;")
        box.execute("CREATE INDEX i1 ON t1 (a);")
        box.execute("CREATE INDEX i2 ON t1 (b);")
        box.execute("CREATE INDEX i3 ON t1 (c);")
        box.execute("CREATE INDEX i4 ON t1 (id, c, b, a, d);")
        local res = box.space.t1.index.i1.parts
        local exp = {{
            fieldno = 2,
            sort_order = 'asc',
            type = 'number',
            exclude_null = false,
            is_nullable = true,
        }}
        t.assert_equals(res, exp)
        res = box.space.t1.index.i2.parts
        exp = {{
            fieldno = 3,
            sort_order = 'asc',
            type = 'integer',
            exclude_null = false,
            is_nullable = true,
        }}
        t.assert_equals(res, exp)
        res = box.space.t1.index.i3.parts
        exp = {{
            fieldno = 4,
            sort_order = 'asc',
            type = 'string',
            exclude_null = false,
            is_nullable = true,
        }}
        t.assert_equals(res, exp)
        res = box.space.t1.index.i4.parts
        exp = {{
            fieldno = 1,
            sort_order = 'asc',
            type = 'string',
            exclude_null = false,
            is_nullable = false,
        },
        {
            fieldno = 4,
            sort_order = 'asc',
            type = 'string',
            exclude_null = false,
            is_nullable = true,
        },
        {
            fieldno = 3,
            sort_order = 'asc',
            type = 'integer',
            exclude_null = false,
            is_nullable = true,
        },
        {
            fieldno = 2,
            sort_order = 'asc',
            type = 'number',
            exclude_null = false,
            is_nullable = true,
        },
        {
            fieldno = 5,
            sort_order = 'asc',
            type = 'scalar',
            exclude_null = false,
            is_nullable = true,
        }}
        t.assert_equals(res, exp)

        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")
    end)
end

--
-- gh-3906: data of type BOOL is displayed as should
-- during SQL SELECT.
--
g.test_3906 = function(cg)
    cg.server:exec(function()
        local format = {
            {name = "id", type = "unsigned"},
            {name = "a", type = "boolean"},
        }
        local sp = box.schema.space.create("test", {format = format})
        sp:create_index('primary', {parts = {1, 'unsigned'}})
        sp:insert({1, true})
        sp:insert({2, false})
        t.assert_equals(box.execute("SELECT * FROM test;").metadata, format)
        sp:drop()
    end)
end

--
-- gh-3544: concatenation operator accepts only TEXT and BLOB.
--
g.test_3544 = function(cg)
    cg.server:exec(function()
        local _, err = box.execute("SELECT 'abc' || 1;")
        local exp_err = "Inconsistent types: expected string or " ..
                        "varbinary got integer(1)"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT 1 || 'abc';")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECt 'a' || 'b' || 1;")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT 'abc' || 1.123;")
        exp_err = "Inconsistent types: expected string or " ..
                  "varbinary got decimal(1.123)"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT 1.123 || 'abc';")
        t.assert_equals(err.message, exp_err)
        -- What is more, they must be of the same type.
        _, err = box.execute("SELECT 'abc' || RANDOMBLOB(5);")
        exp_err = "Inconsistent types: expected string got " ..
                  "varbinary(x'819192E578')"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT RANDOMBLOB(5) || 'x';")
        exp_err = "Inconsistent types: expected varbinary got string('x')"
        t.assert_equals(err.message, exp_err)
        -- Result of BLOBs concatenation must be BLOB.
        local res = box.execute([[VALUES (TYPEOF(RANDOMBLOB(5) ||
                                                 ZEROBLOB(5)));]])
        local exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {'varbinary'},
            },
        }
        t.assert_equals(res, exp)
    end)
end

--
-- gh-3954: LIKE accepts only arguments of type TEXT and NULLs.
--
g.test_3954 = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1 (s SCALAR PRIMARY KEY);")
        box.execute("INSERT INTO t1 VALUES (RANDOMBLOB(5));")
        local _, err = box.execute("SELECT * FROM t1 WHERE s LIKE 'blob';")
        local exp_err = "Failed to execute SQL statement: wrong arguments " ..
                        "for function LIKE()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM t1 WHERE 'blob' LIKE s;")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM t1 WHERE 'blob' LIKE x'0000';")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT s LIKE NULL FROM t1;")
        t.assert_equals(err.message, exp_err)
        box.execute("DELETE FROM t1;")
        box.execute("INSERT INTO t1 VALUES (1);")
        _, err = box.execute("SELECT * FROM t1 WHERE s LIKE 'int';")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM t1 WHERE 'int' LIKE 4;")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT NULL LIKE s FROM t1;")
        t.assert_equals(err.message, exp_err)
        box.space.t1:drop()
    end)
end

--
-- gh-4229: allow explicit cast from string to integer for string
-- values containing quoted floating point literals.
--
g.test_4229 = function(cg)
    cg.server:exec(function()
        local _, err = box.execute("SELECT CAST('1.123' AS INTEGER);")
        local exp_err = "Type mismatch: can not convert string('1.123') " ..
                        "to integer"
        t.assert_equals(err.message, exp_err)
        box.execute("CREATE TABLE t1 (f TEXT PRIMARY KEY);")
        box.execute("INSERT INTO t1 VALUES('0.0'), ('1.5'), ('3.9312453');")
        _, err = box.execute("SELECT CAST(f AS INTEGER) FROM t1;")
        exp_err = "Type mismatch: can not convert string('0.0') to integer"
        t.assert_equals(err.message, exp_err)
        box.space.t1:drop()
    end)
end

--
-- gh-4103: If resulting value of arithmetic operations is
-- integers, then make sure its type also integer (not number).
--
g.test_4103 = function(cg)
    cg.server:exec(function()
        local exp = {{
            name = 'COLUMN_1',
            type = 'integer'
        }}
        t.assert_equals(box.execute('SELECT 1 + 1;').metadata, exp)
        exp = {{
            name = 'COLUMN_1',
            type = 'decimal'
        }}
        t.assert_equals(box.execute('SELECT 1 + 1.1;').metadata, exp)
        local _ , err = box.execute("SELECT '9223372036854' + 1;")
        local exp_err = "Type mismatch: can not convert " ..
                        "string('9223372036854') to integer, decimal, " ..
                        "double, datetime or interval"
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- Fix BOOLEAN bindings.
--
g.test_boolean_binding = function(cg)
    cg.server:exec(function()
        local exp = {
            metadata = {
                {name = "COLUMN_1", type = "boolean"},
            },
            rows = {
                {true},
            },
        }
        t.assert_equals(box.execute('SELECT ?;', {true}), exp)
    end)
end

--
-- gh-4187: make sure that value passed to the iterator has
-- the same type as indexed fields.
--
g.test_4187 = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE tboolean (s1 BOOLEAN PRIMARY KEY);")
        box.execute("INSERT INTO tboolean VALUES (TRUE);")
        local _, err = box.execute("SELECT * FROM tboolean WHERE s1 = x'44';")
        local exp_err = "Type mismatch: can not convert " ..
                        "varbinary(x'44') to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM tboolean WHERE s1 = 'abc';")
        exp_err = "Type mismatch: can not convert string('abc') to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM tboolean WHERE s1 = 1;")
        exp_err = "Type mismatch: can not convert integer(1) to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM tboolean WHERE s1 = 1.123;")
        exp_err = "Type mismatch: can not convert decimal(1.123) to boolean"
        t.assert_equals(err.message, exp_err)

        box.space.tboolean:drop()
    end)
end

--
-- Make sure that indexed field of integer type and
-- floating point value can be compared
--
g.test_integer_floating_point_comparison = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT UNIQUE);")
        box.execute("INSERT INTO t1 VALUES (1, 1);")
        local res = box.execute("SELECT a FROM t1 WHERE a IN (1.1, 2.1);")
        local metadata = {{name = "a", type = "integer"}}
        t.assert_equals(res, {metadata = metadata, rows = {}})
        res = box.execute("SELECT a FROM t1 WHERE a = 1.1;")
        t.assert_equals(res, {metadata = metadata, rows = {}})
        res = box.execute("SELECT a FROM t1 WHERE a = 1.0;")
        t.assert_equals(res, {metadata = metadata, rows = {{1}}})
        res = box.execute("SELECT a FROM t1 WHERE a > 1.1;")
        t.assert_equals(res, {metadata = metadata, rows = {}})
        res = box.execute("SELECT a FROM t1 WHERE a < 1.1;")
        t.assert_equals(res, {metadata = metadata, rows = {{1}}})

        box.space.t1:drop()

        box.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT, b INT);")
        box.execute("CREATE INDEX i1 ON t1(a, b);")
        box.execute("INSERT INTO t1 VALUES (1, 1, 1);")
        res = box.execute("SELECT a FROM t1 WHERE a = 1.0 AND b > 0.5;")
        t.assert_equals(res, {metadata = metadata, rows = {{1}}})
        res = box.execute("SELECT a FROM t1 WHERE a = 1.5 AND b IS NULL;")
        t.assert_equals(res, {metadata = metadata, rows = {}})
        res = box.execute("SELECT a FROM t1 WHERE a IS NULL AND b IS NULL;")
        t.assert_equals(res, {metadata = metadata, rows = {}})

        box.space.t1:drop()

        local format = {}
        format[1] = {name = 'id', type = 'unsigned'}
        format[2] = {name = 'a', type = 'unsigned'}
        local s = box.schema.create_space('t1', {format = format})
        local _ = s:create_index('pk')
        _ = s:create_index('sk', {parts = {'a'}})
        s:insert({1, 1})
        res = box.execute("SELECT a FROM t1 WHERE a IN (1.1, 2.1);")
        metadata = {{name = "a", type = "unsigned"}}
        t.assert_equals(res, {metadata = metadata, rows = {{1}}})

        s:drop()
    end)
end

--
-- gh-3810: range of integer is extended up to 2^64 - 1.
--
g.test_3810 = function(cg)
    cg.server:exec(function()
        local sql = "SELECT 18446744073709551615 > 18446744073709551614;"
        t.assert_equals(box.execute(sql).rows, {{true}})
        sql = "SELECT 18446744073709551615 > -9223372036854775808;"
        t.assert_equals(box.execute(sql).rows, {{true}})
        local res = box.execute("SELECT -1 < 18446744073709551615;")
        t.assert_equals(res.rows, {{true}})
        res = box.execute("SELECT 1.5 < 18446744073709551615;")
        t.assert_equals(res.rows, {{true}})
        res = box.execute("SELECT 1.5 > 18446744073709551615;")
        t.assert_equals(res.rows, {{false}})
        res = box.execute("SELECT 18446744073709551615 > 1.5;")
        t.assert_equals(res.rows, {{true}})
        res = box.execute("SELECT 18446744073709551615 < 1.5;")
        t.assert_equals(res.rows, {{false}})
        res = box.execute("SELECT 18446744073709551615 = 18446744073709551615;")
        t.assert_equals(res.rows, {{true}})
        res = box.execute("SELECT 18446744073709551615 > -9223372036854775808;")
        t.assert_equals(res.rows, {{true}})
        res = box.execute("SELECT 18446744073709551615 < -9223372036854775808;")
        t.assert_equals(res.rows, {{false}})
        res = box.execute("SELECT -1 < 18446744073709551615;")
        t.assert_equals(res.rows, {{true}})
        res = box.execute("SELECT -1 > 18446744073709551615;")
        t.assert_equals(res.rows, {{false}})
        res = box.execute("SELECT 18446744073709551610 - 18446744073709551615;")
        t.assert_equals(res.rows, {{-5}})
        res = box.execute("SELECT 18446744073709551615 = NULL;")
        t.assert_equals(res.rows, {{nil}})
        sql = "SELECT 18446744073709551615 = 18446744073709551615e0;"
        t.assert_equals(box.execute(sql).rows, {{false}})
        sql = "SELECT 18446744073709551615e0 > 18446744073709551615;"
        t.assert_equals(box.execute(sql).rows, {{true}})
        sql = [[SELECT 18446744073709551615 IN ('18446744073709551615',
                                                18446744073709551615e0);]]
        local _, err = box.execute(sql)
        local exp_err = "Type mismatch: can not convert " ..
                        "integer(18446744073709551615) to string"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT 1 LIMIT 18446744073709551615;")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("SELECT 1 LIMIT 1 OFFSET 18446744073709551614;")
        t.assert_equals(res.rows, {})
        sql = "SELECT CAST('18446744073' || '709551616' AS INTEGER);"
        _, err = box.execute(sql)
        exp_err = "Type mismatch: can not convert " ..
                  "string('18446744073709551616') to integer"
        t.assert_equals(err.message, exp_err)
        sql = "SELECT CAST('18446744073' || '709551615' AS INTEGER);"
        t.assert_equals(box.execute(sql).rows, {{18446744073709551615ULL}})
        res = box.execute("SELECT 18446744073709551610 + 5;")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        res = box.execute("SELECT 18446744073709551615 * 1;")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        res = box.execute("SELECT 1 / 18446744073709551615;")
        t.assert_equals(res.rows, {{0}})
        res = box.execute("SELECT 18446744073709551615 / 18446744073709551615;")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("SELECT 18446744073709551615 / -9223372036854775808;")
        t.assert_equals(res.rows, {{-1}})
        _, err = box.execute("SELECT 0 - 18446744073709551610;")
        exp_err = "Failed to execute SQL statement: integer is overflowed"
        t.assert_equals(err.message, exp_err)
        box.execute("CREATE TABLE t (id INT PRIMARY KEY, i INT);")
        box.execute("INSERT INTO t VALUES (1, 18446744073709551615);")
        box.execute("INSERT INTO t VALUES (2, 18446744073709551614);")
        box.execute("INSERT INTO t VALUES (3, 18446744073709551613);")
        res = box.execute("SELECT i FROM t;")
        local exp = {
            {18446744073709551615ULL},
            {18446744073709551614ULL},
            {18446744073709551613ULL},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT i FROM t WHERE i = 18446744073709551615;")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        res = box.execute([[SELECT i FROM t WHERE i BETWEEN 18446744073709551613
                            AND 18446744073709551615;]])
        exp = {
            {18446744073709551615ULL},
            {18446744073709551614ULL},
            {18446744073709551613ULL},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT i FROM t ORDER BY i;")
        exp = {
            {18446744073709551613ULL},
            {18446744073709551614ULL},
            {18446744073709551615ULL},
        }
        t.assert_equals(res.rows, exp)
        _, err = box.execute("SELECT i FROM t ORDER BY -i;")
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT i FROM t ORDER BY i LIMIT 1;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})
        -- Test that built-in functions are capable of handling unsigneds.
        box.execute("DELETE FROM t WHERE i > 18446744073709551613;")
        box.execute("INSERT INTO t VALUES (1, 1);")
        box.execute("INSERT INTO t VALUES (2, -1);")
        res = box.execute("SELECT SUM(i) FROM t;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})
        res = box.execute("SELECT AVG(i) FROM t;")
        t.assert_equals(res.rows, {{6148914691236517204ULL}})
        res = box.execute("SELECT TOTAL(i) FROM t;")
        t.assert_equals(res.rows, {{18446744073709551613}})
        res = box.execute("SELECT MIN(i) FROM t;")
        t.assert_equals(res.rows, {{-1}})
        res = box.execute("SELECT MAX(i) FROM t;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})
        res = box.execute("SELECT COUNT(i) FROM t;")
        t.assert_equals(res.rows, {{3}})
        _, err = box.execute("SELECT GROUP_CONCAT(i) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function GROUP_CONCAT()"
        t.assert_equals(err.message, exp_err)

        box.execute("DELETE FROM t WHERE i < 18446744073709551613;")
        _, err = box.execute("SELECT LOWER(i) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function LOWER()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT UPPER(i) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function UPPER()"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT ABS(i) FROM t;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})
        res = box.execute("SELECT TYPEOF(i) FROM t;")
        t.assert_equals(res.rows, {{'integer'}})
        res = box.execute("SELECT QUOTE(i) FROM t;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})
        res = box.execute("SELECT LEAST(-1, i) FROM t;")
        t.assert_equals(res.rows, {{-1}})
        res = box.execute("SELECT QUOTE(i) FROM t;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})

        box.execute("CREATE INDEX i ON t(i);")
        res = box.execute("SELECT i FROM t WHERE i = 18446744073709551613;")
        t.assert_equals(res.rows, {{18446744073709551613ULL}})
        res = box.execute([[SELECT i FROM t WHERE i >= 18446744073709551613
                            ORDER BY i;]])
        t.assert_equals(res.rows, {{18446744073709551613ULL}})

        box.execute([[UPDATE t SET i = 18446744073709551615 WHERE
                      i = 18446744073709551613;]])
        res = box.execute("SELECT i FROM t;")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})

        -- Test constraints functionality.
        box.execute("CREATE TABLE parent (id INT PRIMARY KEY, a INT UNIQUE);")
        box.execute("INSERT INTO parent VALUES (1, 18446744073709551613);")
        box.space.t:truncate()
        box.execute([[ALTER TABLE t ADD CONSTRAINT fk1 FOREIGN KEY (i)
                      REFERENCES parent (a);]])
        _, err = box.execute("INSERT INTO t VALUES (1, 18446744073709551615);")
        exp_err = "Foreign key constraint 'fk1' failed: " ..
                  "foreign tuple was not found"
        t.assert_equals(err.message, exp_err)
        box.execute("INSERT INTO parent VALUES (2, 18446744073709551615);")
        box.execute("INSERT INTO t VALUES (1, 18446744073709551615);")
        box.execute("ALTER TABLE t DROP CONSTRAINT fk1;")
        box.space.parent:drop()
        box.space.t:drop()

        box.execute([[CREATE TABLE t1 (id INT PRIMARY KEY, a INT CHECK
                      (a > 18446744073709551612));]])
        _, err = box.execute("INSERT INTO t1 VALUES (1, 18446744073709551611);")
        exp_err = "Check constraint 'ck_unnamed_t1_a_1' " ..
                  "failed for field '2 (a)'"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("INSERT INTO t1 VALUES (1, -1);")
        t.assert_equals(err.message, exp_err)
        box.space.t1:drop()
        box.func.check_t1_ck_unnamed_t1_a_1:drop()

        box.execute([[CREATE TABLE t1 (id INT PRIMARY KEY, a INT DEFAULT
                      18446744073709551615);]])
        box.execute("INSERT INTO t1 (id) VALUES (1);")
        res = box.space.t1:select()
        t.assert_equals(res, {{1, 18446744073709551615ULL}})
        box.space.t1:drop()

        -- Test that autoincrement accepts only max 2^63 - 1 .
        box.execute("CREATE TABLE t1 (id INT PRIMARY KEY AUTOINCREMENT);")
        res = box.execute("INSERT INTO t1 VALUES (18446744073709551615);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO t1 VALUES (NULL);")
        exp = {
            autoincrement_ids = {1},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        box.space.t1:drop()

        -- Test CAST facilities.
        res = box.execute("SELECT CAST(18446744073709551615 AS NUMBER);")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        res = box.execute("SELECT CAST(18446744073709551615 AS TEXT);")
        t.assert_equals(res.rows, {{'18446744073709551615'}})
        res = box.execute("SELECT CAST(18446744073709551615 AS SCALAR);")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        _, err = box.execute("SELECT CAST(18446744073709551615 AS BOOLEAN);")
        exp_err = "Type mismatch: can not convert " ..
                  "integer(18446744073709551615) to boolean"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT CAST('18446744073709551615' AS INTEGER);")
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
    end)
end

--
-- gh-4015: introduce unsigned type in SQL.
--
g.test_4015 = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1 (id UNSIGNED PRIMARY KEY);")
        box.execute("INSERT INTO t1 VALUES (0), (1), (2);")
        local _, err = box.execute("INSERT INTO t1 VALUES (-3);")
        local exp_err = "Type mismatch: can not convert integer(-3) to unsigned"
        t.assert_equals(err.message, exp_err)
        local res = box.execute("SELECT id FROM t1;")
        t.assert_equals(res.rows, {{0}, {1}, {2}})

        res = box.execute("SELECT CAST(123 AS UNSIGNED);")
        t.assert_equals(res.metadata, {{name = "COLUMN_1", type = "unsigned"}})
        _, err = box.execute("SELECT CAST(-123 AS UNSIGNED);")
        exp_err = "Type mismatch: can not convert integer(-123) to unsigned"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT CAST(1.5 AS UNSIGNED);")
        t.assert_equals(res.metadata, {{name = "COLUMN_1", type = "unsigned"}})
        _, err = box.execute("SELECT CAST(-1.5 AS UNSIGNED);")
        exp_err = "Type mismatch: can not convert decimal(-1.5) to unsigned"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT CAST(true AS UNSIGNED);")
        exp_err = "Type mismatch: can not convert boolean(TRUE) to unsigned"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT CAST('123' AS UNSIGNED);")
        t.assert_equals(res.metadata, {{name = "COLUMN_1", type = "unsigned"}})
        _, err = box.execute("SELECT CAST('-123' AS UNSIGNED);")
        exp_err = "Type mismatch: can not convert string('-123') to unsigned"
        t.assert_equals(err.message, exp_err)

        box.space.t1:drop()
    end)
end

--
-- Check that STRING is a valid alias to TEXT type.
--
g.test_text_as_string_alias = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t (id STRING PRIMARY KEY);")
        t.assert_equals(box.space.t:format()[1].type, 'string')
        box.space.t:drop()
    end)
end

--
-- Make sure that CASE-THEN statement return type is SCALAR in
-- case two THEN clauses feature different types.
--
g.test_case_then = function(cg)
    cg.server:exec(function()
        local res = box.execute([[SELECT CASE 1 WHEN 1 THEN x'0000000000'
                                                WHEN 2 THEN 'str' END;]])
        t.assert_equals(res.metadata[1].type, "scalar")
        res = box.execute("SELECT CASE 1 WHEN 1 THEN 666 WHEN 2 THEN 123 END;")
        t.assert_equals(res.metadata[1].type, "integer")
        res = box.execute([[SELECT CASE 1 WHEN 1 THEN 666
                                          WHEN 2 THEN 123 ELSE 321 END;]])
        t.assert_equals(res.metadata[1].type, "integer")
        res = box.execute([[SELECT CASE 1 WHEN 1 THEN 666
                                          WHEN 2 THEN 123 ELSE 'asd' END;]])
        t.assert_equals(res.metadata[1].type, "scalar")
        res = box.execute([[SELECT CASE 'a' WHEN 'a' THEN 1
                                            WHEN 'b' THEN 2
                                            WHEN 'c' THEN 3
                                            WHEN 'd' THEN 4
                                            WHEN 'e' THEN 5
                                            WHEN 'f' THEN 6 END;]])
        t.assert_equals(res.metadata[1].type, "integer")
        res = box.execute([[SELECT CASE 'a' WHEN 'a' THEN 1
                                            WHEN 'b' THEN 2
                                            WHEN 'c' THEN 3
                                            WHEN 'd' THEN 4
                                            WHEN 'e' THEN 5
                                            WHEN 'f' THEN 'asd' END;]])
        t.assert_equals(res.metadata[1].type, "scalar")
        res = box.execute([[SELECT CASE 'a' WHEN 'a' THEN 1
                                            WHEN 'b' THEN 2
                                            WHEN 'c' THEN 3
                                            WHEN 'd' THEN 4
                                            WHEN 'e' THEN 5
                                            WHEN 'f' THEN 6 ELSE 'asd' END;]])
        t.assert_equals(res.metadata[1].type, "scalar")
        res = box.execute([[SELECT CASE 'a' WHEN 'a' THEN 1
                                            WHEN 'b' THEN 2
                                            WHEN 'c' THEN 3
                                            WHEN 'd' THEN 4
                                            WHEN 'e' THEN 5
                                            WHEN 'f' THEN 6 ELSE 7 END;]])
        t.assert_equals(res.metadata[1].type, "integer")
    end)
end

--
-- Test basic capabilities of VARBINARY type.
--
g.test_varbinary = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t (id INT PRIMARY KEY, v VARBINARY);")
        local _, err = box.execute("INSERT INTO t VALUES(1, 1);")
        local exp_err = "Type mismatch: can not convert integer(1) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("INSERT INTO t VALUES(1, 1.123);")
        exp_err = "Type mismatch: can not convert decimal(1.123) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("INSERT INTO t VALUES(1, true);")
        exp_err = "Type mismatch: can not convert boolean(TRUE) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("INSERT INTO t VALUES(1, 'asd');")
        exp_err = "Type mismatch: can not convert string('asd') to varbinary"
        t.assert_equals(err.message, exp_err)
        box.execute("INSERT INTO t VALUES(1, x'616263');")
        _, err = box.execute("SELECT * FROM t WHERE v = 1;")
        exp_err = "Type mismatch: can not convert integer(1) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM t WHERE v = 1.123;")
        exp_err = "Type mismatch: can not convert decimal(1.123) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT * FROM t WHERE v = 'str';")
        exp_err = "Type mismatch: can not convert string('str') to varbinary"
        t.assert_equals(err.message, exp_err)
        local res = box.execute("SELECT * FROM t WHERE v = x'616263';")
        local exp = {
            metadata = {
                {name = "id", type = "integer"},
                {name = "v", type = "varbinary"},
            },
            rows = {
                {1, "\x61\x62\x63"},
            },
        }
        t.assert_equals(res, exp)

        _, err = box.execute("SELECT SUM(v) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function SUM()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT AVG(v) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function AVG()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT TOTAL(v) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function TOTAL()"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT MIN(v) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "varbinary"}},
            rows = {{"\x61\x62\x63"}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT MAX(v) FROM t;")
        t.assert_equals(res, exp)
        res = box.execute("SELECT GROUP_CONCAT(v) FROM t;")
        t.assert_equals(res, exp)
        res = box.execute("SELECT COUNT(v) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "integer"}},
            rows = {{1}},
        }
        t.assert_equals(res, exp)

        _, err = box.execute("SELECT LOWER(v) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function LOWER()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT UPPER(v) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function UPPER()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT ABS(v) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments " ..
                  "for function ABS()"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT TYPEOF(v) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "string"}},
            rows = {{'varbinary'}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT QUOTE(v) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "string"}},
            rows = {{"X'616263'"}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT LEAST(v, x'') FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "varbinary"}},
            rows = {{""}},
        }
        t.assert_equals(res, exp)

        box.execute("CREATE INDEX iv ON t(v);")
        res = box.execute("SELECT v FROM t WHERE v = x'616263';")
        exp = {
            metadata = {{name = "v", type = "varbinary"}},
            rows = {{"\x61\x62\x63"}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT v FROM t ORDER BY v;")
        t.assert_equals(res, exp)

        box.execute("UPDATE t SET v = x'636261' WHERE v = x'616263';")
        res = box.execute("SELECT v FROM t;")
        exp = {
            metadata = {{name = "v", type = "varbinary"}},
            rows = {{"\x63\x62\x61"}},
        }
        t.assert_equals(res, exp)

        box.execute([[CREATE TABLE parent (id INT PRIMARY KEY,
                                           a VARBINARY UNIQUE);]])
        box.space.t:truncate()
        box.execute([[ALTER TABLE t ADD CONSTRAINT fk1 FOREIGN KEY (v)
                      REFERENCES parent (a);]])
        _, err = box.execute("INSERT INTO t VALUES (1, x'616263');")
        exp_err = "Foreign key constraint 'fk1' failed: foreign tuple was " ..
                  "not found"
        t.assert_equals(err.message, exp_err)
        box.execute("INSERT INTO parent VALUES (1, x'616263');")
        box.execute("INSERT INTO t VALUES (1, x'616263');")
        box.execute("ALTER TABLE t DROP CONSTRAINT fk1;")
        box.space.parent:drop()
        box.space.t:drop()

        box.execute([[CREATE TABLE t1 (id INT PRIMARY KEY, a VARBINARY CHECK
                      (a = x'616263'));]])
        _, err = box.execute("INSERT INTO t1 VALUES (1, x'006162');")
        exp_err = "Check constraint 'ck_unnamed_t1_a_1' failed for " ..
                  "field '2 (a)'"
        t.assert_equals(err.message, exp_err)
        box.execute("INSERT INTO t1 VALUES (1, x'616263');")
        box.space.t1:drop()
        box.func.check_t1_ck_unnamed_t1_a_1:drop()

        box.execute([[CREATE TABLE t1 (id INT PRIMARY KEY, a VARBINARY DEFAULT
                      x'616263');]])
        box.execute("INSERT INTO t1 (id) VALUES (1);")
        res = box.space.t1:select()
        t.assert_equals(res, {{1, "\x61\x62\x63"}})
        box.space.t1:drop()

        _, err = box.execute("SELECT CAST(1 AS VARBINARY);")
        exp_err = "Type mismatch: can not convert integer(1) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT CAST(1.123 AS VARBINARY);")
        exp_err = "Type mismatch: can not convert decimal(1.123) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT CAST(true AS VARBINARY);")
        exp_err = "Type mismatch: can not convert boolean(TRUE) to varbinary"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT CAST('asd' AS VARBINARY);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "varbinary"}},
            rows = {{"\x61\x73\x64"}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT CAST(x'' AS VARBINARY);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "varbinary"}},
            rows = {{""}},
        }
        t.assert_equals(res, exp)
    end)
end

--
-- gh-4189: make sure that update doesn't throw an error if format
-- of table features map/array field types.
--
g.test_4189 = function(cg)
    cg.server:exec(function()
        local format = {}
        format[1] = {type = 'integer', name = 'i'}
        format[2] = {type = 'boolean', name = 'b'}
        format[3] = {type = 'array', name = 'f1'}
        format[4] = {type = 'map', name = 'f2'}
        format[5] = {type = 'any', name = 'f3'}
        local s = box.schema.space.create('t', {format = format})
        s:create_index('ii')
        s:insert({1, true, {1, 2}, {a = 3}, 'asd'})
        local res = box.execute('UPDATE t SET b = false WHERE i = 1;')
        t.assert_equals(res, {row_count = 1})
        res = s:select()
        t.assert_equals(res, {{1, false, {1, 2}, {a = 3}, 'asd'}})
        s:drop()
    end)
end

--
-- Make sure that the array/map conversion to scalar error is
-- displayed correctly.
--
g.test_4189 = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1(i INT PRIMARY KEY AUTOINCREMENT,
                                      a SCALAR);]])
        local format = {}
        format[1] = {type = 'integer', name = 'i'}
        format[2] = {type = 'array', name = 'a'}
        local s = box.schema.space.create('t2', {format = format})
        s:create_index('ii')
        s:insert({1, {1,2,3}})
        local _, err = box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
        local exp_err = "Type mismatch: can not convert array([1, 2, 3]) " ..
                        "to scalar"
        t.assert_equals(err.message, exp_err)
        local m = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                   18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30}
        s:replace({1, m})
        _, err = box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
        exp_err = "Type mismatch: can not convert array([1, 2, 3, 4, 5, 6, " ..
                  "7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, " ..
                  "22, 23, 24, 25, 26, 27, 28, 29, 30]) to scalar"
        t.assert_equals(err.message, exp_err)

        -- Make sure that the error will be displayed correctly even if
        -- the value is too long.
        local long_array = {}
        for i = 1,120 do long_array[i] = i end
        s:replace({1, long_array})
        _, err = box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
        exp_err = "Type mismatch: can not convert array([1, 2, 3, 4, 5, 6, " ..
                  "7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, " ..
                  "22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,...) " ..
                  "to scalar"
        t.assert_equals(err.message, exp_err)
        s:drop()
        format[2].type = 'map'
        s = box.schema.space.create('t2', {format = format})
        s:create_index('ii')
        s:insert({1, {b = 1}})
        _, err = box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
        exp_err = 'Type mismatch: can not convert map({"b": 1}) to scalar'
        t.assert_equals(err.message, exp_err)
        s:drop()
        box.execute('DROP TABLE t1;')
    end)
end














--
-- gh-3812: Make sure DOUBLE type works correctly.
--
g.test_3812 = function(cg)
    cg.server:exec(function()
        local res = box.execute("SELECT 1.0e0;")
        local exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{1.0}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT 1e-2;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{0.01}},
        }
        t.assert_equals(res, exp)

        res = box.execute("SELECT CAST(1 AS DOUBLE);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{1}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT CAST(1.123 AS DOUBLE);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{1.123}},
        }
        t.assert_equals(res, exp)
        local _, err = box.execute("SELECT CAST(true AS DOUBLE);")
        local exp_err = "Type mismatch: can not convert boolean(TRUE) to double"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT CAST('asd' AS DOUBLE);")
        exp_err = "Type mismatch: can not convert string('asd') to double"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT CAST('1' AS DOUBLE);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{1}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT CAST('1.123' AS DOUBLE);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{1.123}},
        }
        t.assert_equals(res, exp)
        _, err = box.execute("SELECT CAST(x'' AS DOUBLE);")
        exp_err = "Type mismatch: can not convert varbinary(x'') to double"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT CAST(x'35' AS DOUBLE);")
        exp_err = "Type mismatch: can not convert varbinary(x'35') to double"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT CAST(CAST(x'35' AS STRING) AS DOUBLE);")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{5}},
        }
        t.assert_equals(res, exp)

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT,
                      d DOUBLE);]])
        box.execute([[INSERT INTO t(d) VALUES (10), (-2.0e0), (3.3e0),
                                              (18000000000000000000);]])
        res = box.execute('SELECT * FROM t;')
        exp = {
            metadata = {
                {name = "i", type = "integer"},
                {name = "d", type = "double"},
            },
            rows = {
                {1, 10},
                {2, -2},
                {3, 3.3},
                {4, 18000000000000000000},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute('SELECT d / 100 FROM t;')
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "double"},
            },
            rows = {
                {0.1},
                {-0.02},
                {0.033},
                {180000000000000000},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute('SELECT * from t WHERE d < 15;')
        exp = {
            metadata = {
                {name = "i", type = "integer"},
                {name = "d", type = "double"},
            },
            rows = {
                {1, 10},
                {2, -2},
                {3, 3.3},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute('SELECT * from t WHERE d = 3.3e0;')
        exp = {
            metadata = {
                {name = "i", type = "integer"},
                {name = "d", type = "double"},
            },
            rows = {{3, 3.3}},
        }
        t.assert_equals(res, exp)

        res = box.execute("SELECT SUM(d) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{18000000000000000000}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT AVG(d) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{4500000000000000000}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT TOTAL(d) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{18000000000000000000}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT MIN(d) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{-2}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT MAX(d) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "double"}},
            rows = {{18000000000000000000}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT COUNT(d) FROM t;")
        exp = {
            metadata = {{name = "COLUMN_1", type = "integer"}},
            rows = {{4}},
        }
        t.assert_equals(res, exp)
        _, err = box.execute("SELECT GROUP_CONCAT(d) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments for " ..
                  "function GROUP_CONCAT()"
        t.assert_equals(err.message, exp_err)

        _, err = box.execute("SELECT LOWER(d) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments for " ..
                  "function LOWER()"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("SELECT UPPER(d) FROM t;")
        exp_err = "Failed to execute SQL statement: wrong arguments for " ..
                  "function UPPER()"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT ABS(d) FROM t;")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "double"},
            },
            rows = {
                {10},
                {2},
                {3.3},
                {18000000000000000000},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT TYPEOF(d) FROM t;")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {'double'},
                {'double'},
                {'double'},
                {'double'},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT QUOTE(d) FROM t;")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {10},
                {-2},
                {3.3},
                {18000000000000000000},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT LEAST(d, 0) FROM t;")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "number"},
            },
            rows = {
                {0},
                {-2},
                {0},
                {0},
            },
        }
        t.assert_equals(res, exp)

        box.execute("CREATE INDEX dd ON t(d);")
        res = box.execute("SELECT d FROM t WHERE d < 0;")
        exp = {
            metadata = {{name = "d", type = "double"}},
            rows = {{-2}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT d FROM t ORDER BY d;")
        exp = {
            metadata = {{name = "d", type = "double"}},
            rows = {
                {-2},
                {3.3},
                {10},
                {18000000000000000000},
            },
        }
        t.assert_equals(res, exp)

        box.execute("UPDATE t SET d = 1 WHERE d = 10;")
        res = box.execute("SELECT d FROM t;")
        exp = {
            metadata = {
                {name = "d", type = "double"},
            },
            rows = {
                {1},
                {-2},
                {3.3},
                {18000000000000000000},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t;")

        box.execute("CREATE TABLE t1 (d DOUBLE PRIMARY KEY);")
        box.execute("INSERT INTO t1 VALUES (1), (2.2e0), (3.5e0);")
        _, err = box.execute("INSERT INTO t1 VALUES (1);")
        exp_err = 'Duplicate key exists in unique index "pk_unnamed_t1_1" ' ..
                  'in space "t1" with old tuple - [1] and new tuple - [1]'
        t.assert_equals(err.message, exp_err)

        box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY,
                                       d DOUBLE REFERENCES t1);]])
        box.execute([[INSERT INTO t2 VALUES (1, 1), (2, 2.2e0), (100, 3.5e0),
                                            (4, 1);]])
        _, err = box.execute("INSERT INTO t2 VALUES (5, 10);")
        exp_err = "Foreign key constraint 'fk_unnamed_t2_d_1' failed for " ..
                  "field '2 (d)': foreign tuple was not found"
        t.assert_equals(err.message, exp_err)
        box.execute("DROP TABLE t2;")
        box.execute("DROP TABLE t1;")

        box.execute([[CREATE TABLE t3 (i INT PRIMARY KEY,
                                       d DOUBLE CHECK (d < 10));]])
        box.execute("INSERT INTO t3 VALUES (1, 1);")
        box.execute("INSERT INTO t3 VALUES (2, 9.999999e0);")
        _, err = box.execute("INSERT INTO t3 VALUES (3, 10.0000001e0);")
        exp_err = "Check constraint 'ck_unnamed_t3_d_1' failed for " ..
                  "field '2 (d)'"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3;")
        exp = {
            metadata = {
                {name = "i", type = "integer"},
                {name = "d", type = "double"},
            },
            rows = {
                {1, 1},
                {2, 9.999999},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3;")
        box.func.check_t3_ck_unnamed_t3_d_1:drop()

        box.execute([[CREATE TABLE t4 (i INT PRIMARY KEY,
                                       d DOUBLE DEFAULT 1.2345e0);]])
        box.execute("INSERT INTO t4(i) VALUES (1);")
        res = box.execute("SELECT * FROM t4;")
        exp = {
            metadata = {
                {name = "i", type = "integer"},
                {name = "d", type = "double"},
            },
            rows = {{1, 1.2345}},
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t4;")

        -- Make sure the TYPEOF() function works correctly with DOUBLE.
        res = box.execute("SELECT 1.0e0, TYPEOF(1.0e0);")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "double"},
                {name = "COLUMN_2", type = "string"},
            },
            rows = {{1.0, 'double'}},
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT CAST(2 AS DOUBLE),
                            TYPEOF(CAST(2 AS DOUBLE));]])
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "double"},
                {name = "COLUMN_2", type = "string"},
            },
            rows = {{2, 'double'}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT 3e3, TYPEOF(3e3);")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "double"},
                {name = "COLUMN_2", type = "string"},
            },
            rows = {{3000, 'double'}},
        }
        t.assert_equals(res, exp)

        box.execute("CREATE TABLE t5 (d DOUBLE PRIMARY KEY);")
        box.execute("INSERT INTO t5 VALUES (4), (5.5e0), (6e6);")
        res = box.execute("SELECT d, TYPEOF(d) FROM t5;")
        exp = {
            metadata = {
                {name = "d", type = "double"},
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {4, 'double'},
                {5.5, 'double'},
                {6000000, 'double'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t5;")
    end)
end

--
-- gh-4728: make sure that given query doesn't result in
-- assertion fault.
--
g.test_4728 = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:format({
            {name = 'id', type = 'unsigned'},
            {name = 'v', type = 'string', is_nullable = true},
        })

        local res = box.execute([[SELECT * FROM "s" WHERE "id" = ?;]])
        t.assert_equals(res.rows, {})
        s:drop()
    end)
end

--
-- gh-3809: Make sure there are no implicit casts during
-- assignment, except for the implicit cast between numeric
-- values.
--
g.test_3809 = function(cg)
    cg.server:exec(function()
        -- Check INSERT.
        box.execute([[CREATE TABLE ti (a INT PRIMARY KEY AUTOINCREMENT,
                                       i INTEGER);]])
        box.execute([[CREATE TABLE td (a INT PRIMARY KEY AUTOINCREMENT,
                                       d DOUBLE);]])
        box.execute([[CREATE TABLE tb (a INT PRIMARY KEY AUTOINCREMENT,
                                       b BOOLEAN);]])
        box.execute([[CREATE TABLE tt (a INT PRIMARY KEY AUTOINCREMENT,
                                       t TEXT);]])
        box.execute([[CREATE TABLE tv (a INT PRIMARY KEY AUTOINCREMENT,
                                       v VARBINARY);]])
        box.execute([[CREATE TABLE ts (a INT PRIMARY KEY AUTOINCREMENT,
                                       s SCALAR);]])

        box.execute([[INSERT INTO ti(i) VALUES (NULL);]])
        box.execute([[INSERT INTO ti(i) VALUES (11);]])
        local _, err = box.execute([[INSERT INTO ti(i) VALUES
                                     (100000000000000000000000000000000.1e0);]])
        local exp_err = "Type mismatch: can not convert double(1.0e+32) " ..
                        "to integer"
        t.assert_equals(err.message, exp_err)
        box.execute([[INSERT INTO ti(i) VALUES (33.0);]])
        _, err = box.execute([[INSERT INTO ti(i) VALUES (true);]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to integer"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO ti(i) VALUES ('33');]])
        exp_err = "Type mismatch: can not convert string('33') to integer"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO ti(i) VALUES (X'3434');]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to integer"
        t.assert_equals(err.message, exp_err)
        local res = box.execute([[SELECT * FROM ti;]])
        local exp = {
            {1, nil},
            {2, 11},
            {3, 33},
        }
        t.assert_equals(res.rows, exp)

        box.execute([[INSERT INTO td(d) VALUES (NULL);]])
        box.execute([[INSERT INTO td(d) VALUES (11);]])
        _, err = box.execute([[INSERT INTO td(d) VALUES (100000000000000001);]])
        exp_err = "Type mismatch: can not convert " ..
                  "integer(100000000000000001) to double"
        t.assert_equals(err.message, exp_err)
        box.execute([[INSERT INTO td(d) VALUES (22.2);]])
        _, err = box.execute([[INSERT INTO td(d) VALUES (true);]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to double"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO td(d) VALUES ('33');]])
        exp_err = "Type mismatch: can not convert string('33') to double"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO td(d) VALUES (X'3434');]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to double"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM td;]])
        exp = {
            {1, nil},
            {2, 11},
            {3, 22.2},
        }
        t.assert_equals(res.rows, exp)

        box.execute([[INSERT INTO tb(b) VALUES (NULL);]])
        _, err = box.execute([[INSERT INTO tb(b) VALUES (11);]])
        exp_err = "Type mismatch: can not convert integer(11) to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tb(b) VALUES (22.2);]])
        exp_err = "Type mismatch: can not convert decimal(22.2) to boolean"
        t.assert_equals(err.message, exp_err)
        box.execute([[INSERT INTO tb(b) VALUES (true);]])
        _, err = box.execute([[INSERT INTO tb(b) VALUES ('33');]])
        exp_err = "Type mismatch: can not convert string('33') to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tb(b) VALUES (X'3434');]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to boolean"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM tb;]])
        exp = {
            {1, nil},
            {2, true},
        }
        t.assert_equals(res.rows, exp)

        box.execute([[INSERT INTO tt(t) VALUES (NULL);]])
        _, err = box.execute([[INSERT INTO tt(t) VALUES (11);]])
        exp_err = "Type mismatch: can not convert integer(11) to string"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tt(t) VALUES (22.2);]])
        exp_err = "Type mismatch: can not convert decimal(22.2) to string"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tt(t) VALUES (true);]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to string"
        t.assert_equals(err.message, exp_err)
        box.execute([[INSERT INTO tt(t) VALUES ('33');]])
        _, err = box.execute([[INSERT INTO tt(t) VALUES (X'3434');]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to string"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM tt;]])
        exp = {
            {1, nil},
            {2, '33'},
        }
        t.assert_equals(res.rows, exp)

        box.execute([[INSERT INTO tv(v) VALUES (NULL);]])
        _, err = box.execute([[INSERT INTO tv(v) VALUES (11);]])
        exp_err = "Type mismatch: can not convert integer(11) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tv(v) VALUES (22.2);]])
        exp_err = "Type mismatch: can not convert decimal(22.2) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tv(v) VALUES (true);]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[INSERT INTO tv(v) VALUES ('33');]])
        exp_err = "Type mismatch: can not convert string('33') to varbinary"
        t.assert_equals(err.message, exp_err)
        box.execute([[INSERT INTO tv(v) VALUES (X'3434');]])
        res = box.execute([[SELECT * FROM tv;]])
        exp = {
            {1, nil},
            {2, '\x34\x34'},
        }
        t.assert_equals(res.rows, exp)

        box.execute([[INSERT INTO ts(s) VALUES (NULL);]])
        box.execute([[INSERT INTO ts(s) VALUES (11);]])
        box.execute([[INSERT INTO ts(s) VALUES (22.2);]])
        box.execute([[INSERT INTO ts(s) VALUES (true);]])
        box.execute([[INSERT INTO ts(s) VALUES ('33');]])
        box.execute([[INSERT INTO ts(s) VALUES (X'3434');]])
        res = box.execute([[SELECT * FROM ts;]])
        exp = {
            {1, nil},
            {2, 11},
            {3, 22.2},
            {4, true},
            {5, '33'},
            {6, '\x34\x34'},
        }
        t.assert_equals(res.rows, exp)

        -- Check for UPDATE.
        box.execute([[DELETE FROM ti;]])
        box.execute([[DELETE FROM td;]])
        box.execute([[DELETE FROM tb;]])
        box.execute([[DELETE FROM tt;]])
        box.execute([[DELETE FROM tv;]])
        box.execute([[DELETE FROM ts;]])
        box.execute([[INSERT INTO ti VALUES(1, NULL);]])
        box.execute([[INSERT INTO td VALUES(1, NULL);]])
        box.execute([[INSERT INTO tb VALUES(1, NULL);]])
        box.execute([[INSERT INTO tt VALUES(1, NULL);]])
        box.execute([[INSERT INTO tv VALUES(1, NULL);]])
        box.execute([[INSERT INTO ts VALUES(1, NULL);]])
        res = box.execute([[SELECT * FROM ti, td, tb, tt, tv, ts;]])
        exp = {{1, nil, 1, nil, 1, nil, 1, nil, 1, nil, 1, nil}}
        t.assert_equals(res.rows, exp)

        box.execute([[UPDATE ti SET i = NULL WHERE a = 1;]])
        box.execute([[UPDATE ti SET i = 11 WHERE a = 1;]])
        _, err = box.execute([[UPDATE ti SET
                               i = 100000000000000000000000000000000.1e0
                               WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert double(1.0e+32) to integer"
        t.assert_equals(err.message, exp_err)
        box.execute([[UPDATE ti SET i = 33.0 WHERE a = 1;]])
        _, err = box.execute([[UPDATE ti SET i = true WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to integer"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE ti SET i = '33' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert string('33') to integer"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE ti SET i = X'3434' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to integer"
        t.assert_equals(err.message, exp_err)
        box.execute([[SELECT * FROM ti;]])

        box.execute([[UPDATE td SET d = NULL WHERE a = 1;]])
        box.execute([[UPDATE td SET d = 11 WHERE a = 1;]])
        _, err = box.execute([[UPDATE td SET d = 100000000000000001
                               WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert " ..
                  "integer(100000000000000001) to double"
        t.assert_equals(err.message, exp_err)
        box.execute([[UPDATE td SET d = 22.2 WHERE a = 1;]])
        _, err = box.execute([[UPDATE td SET d = true WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to double"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE td SET d = '33' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert string('33') to double"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE td SET d = X'3434' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to double"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM td;]])
        t.assert_equals(res.rows, {{1, 22.2}})

        box.execute([[UPDATE tb SET b = NULL WHERE a = 1;]])
        _, err = box.execute([[UPDATE tb SET b = 11 WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert integer(11) to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tb SET b = 22.2 WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert decimal(22.2) to boolean"
        t.assert_equals(err.message, exp_err)
        box.execute([[UPDATE tb SET b = true WHERE a = 1;]])
        _, err = box.execute([[UPDATE tb SET b = '33' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert string('33') to boolean"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tb SET b = X'3434' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to boolean"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM tb;]])
        t.assert_equals(res.rows, {{1, true}})

        box.execute([[UPDATE tt SET t = NULL WHERE a = 1;]])
        _, err = box.execute([[UPDATE tt SET t = 11 WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert integer(11) to string"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tt SET t = 22.2 WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert decimal(22.2) to string"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tt SET t = true WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to string"
        t.assert_equals(err.message, exp_err)
        box.execute([[UPDATE tt SET t = '33' WHERE a = 1;]])
        _, err = box.execute([[UPDATE tt SET t = X'3434' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert varbinary(x'3434') to string"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM tt;]])
        t.assert_equals(res.rows, {{1, '33'}})

        box.execute([[UPDATE tv SET v = NULL WHERE a = 1;]])
        _, err = box.execute([[UPDATE tv SET v = 11 WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert integer(11) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tv SET v = 22.2 WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert decimal(22.2) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tv SET v = true WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert boolean(TRUE) to varbinary"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[UPDATE tv SET v = '33' WHERE a = 1;]])
        exp_err = "Type mismatch: can not convert string('33') to varbinary"
        t.assert_equals(err.message, exp_err)
        box.execute([[UPDATE tv SET v = X'3434' WHERE a = 1;]])
        res = box.execute([[SELECT * FROM tv;]])
        t.assert_equals(res.rows, {{1, '\x34\x34'}})

        box.execute([[UPDATE ts SET s = NULL WHERE a = 1;]])
        box.execute([[UPDATE ts SET s = 11 WHERE a = 1;]])
        box.execute([[UPDATE ts SET s = 22.2 WHERE a = 1;]])
        box.execute([[UPDATE ts SET s = true WHERE a = 1;]])
        box.execute([[UPDATE ts SET s = '33' WHERE a = 1;]])
        box.execute([[UPDATE ts SET s = X'3434' WHERE a = 1;]])
        res = box.execute([[SELECT * FROM ts;]])
        t.assert_equals(res.rows, {{1, '\x34\x34'}})

        box.execute([[DROP TABLE ti;]])
        box.execute([[DROP TABLE td;]])
        box.execute([[DROP TABLE tb;]])
        box.execute([[DROP TABLE tt;]])
        box.execute([[DROP TABLE tv;]])
        box.execute([[DROP TABLE ts;]])
    end)
end
