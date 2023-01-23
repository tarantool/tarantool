local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'datetime'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local fmt1 = {{'I', 'integer'}, {'DT', 'datetime'}}
        local t1 = box.schema.space.create('T1', {format = fmt1})
        box.space.T1:create_index('I')

        local fmt2 = {{'DT', 'datetime'}}
        local t2 = box.schema.space.create('T2', {format = fmt2})
        box.space.T2:create_index('I')

        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        t1:insert({1, dt1})
        t1:insert({2, dt2})
        t1:insert({3, dt3})
        t1:insert({4, dt1})
        t1:insert({5, dt2})
        t1:insert({6, dt3})
        t2:insert({dt1})
        t2:insert({dt2})
        t2:insert({dt3})
    end)
end)

g.after_all(function()
    g.server:exec(function()
        box.space.T1:drop()
        box.space.T2:drop()
    end)
    g.server:stop()
end)

-- Make sure that it is possible to create spaces with DATETIME field.
g.test_datetime_1 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT PRIMARY KEY, dt DATETIME);]]
        local res = {row_count = 1}
        t.assert_equals(box.execute(sql), res)
        box.execute([[DROP TABLE t;]])
    end)
end

-- Make sure that DATETIME can be used as primary key.
g.test_datetime_2 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (dt DATETIME PRIMARY KEY);]]
        local res = {row_count = 1}
        t.assert_equals(box.execute(sql), res)
        box.execute([[DROP TABLE t;]])
    end)
end

-- Make sure that DATETIME can be selected.
g.test_datetime_3_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT dt FROM t1;]]
        local res = {{dt1}, {dt2}, {dt3}, {dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_3_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT dt FROM t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that DATETIME can be used in ORDER BY.
g.test_datetime_4_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT * FROM t1 ORDER BY dt;]]
        local res = {{1, dt1}, {4, dt1}, {2, dt2}, {5, dt2}, {3, dt3}, {6, dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_4_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT * FROM t1 ORDER BY dt DESC;]]
        local res = {{3, dt3}, {6, dt3}, {2, dt2}, {5, dt2}, {1, dt1}, {4, dt1}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_4_3 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT * FROM t2 ORDER BY dt;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_4_4 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT * FROM t2 ORDER BY dt DESC;]]
        local res = {{dt3}, {dt2}, {dt1}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that DATETIME can be used in GROUP BY.
g.test_datetime_5_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT count(*), dt FROM t1 GROUP BY dt;]]
        local res = {{2, dt1}, {2, dt2}, {2, dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_5_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT count(*), dt FROM t2 GROUP BY dt;]]
        local res = {{1, dt1}, {1, dt2}, {1, dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that DATETIME can be used in subselects.
g.test_datetime_6 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT * FROM (SELECT * FROM t1 LIMIT 3 OFFSET 1);]]
        local res = {{2, dt2}, {3, dt3}, {4, dt1}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that DATETIME can work with DISTINCT.
g.test_datetime_7 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT DISTINCT dt FROM t1;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that DATETIME can work with VIEW.
g.test_datetime_8 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        box.execute([[CREATE VIEW v AS SELECT dt FROM t1;]])
        local sql = [[SELECT * FROM v;]]
        local res = {{dt1}, {dt2}, {dt3}, {dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows
        box.execute([[DROP VIEW v;]])

        t.assert_equals(rows, res)
    end)
end

-- Make sure that DATETIME cannot be used in LIMIT.
g.test_datetime_9 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t1 LIMIT (SELECT dt FROM t1 LIMIT 1);]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[Only positive integers are allowed in the LIMIT clause]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME cannot be used in OFFSET.
g.test_datetime_10 = function()
    g.server:exec(function()
        local sql =
            [[SELECT * FROM t1 LIMIT 1 OFFSET (SELECT dt FROM t1 LIMIT 1);]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[Only positive integers are allowed in the OFFSET clause]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME can be used in FOREIGN KEY constraint.
g.test_datetime_11 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (id DATETIME PRIMARY KEY,
                                      dt DATETIME REFERENCES t(id));]])

        local sql = [[INSERT INTO t SELECT
                      (SELECT dt from t2 LIMIT 1 OFFSET 1),
                      (SELECT dt from t2 LIMIT 1);]]
        local res = [[Foreign key constraint 'fk_unnamed_T_DT_1' failed for ]]..
                    [[field '2 (DT)': foreign tuple was not found]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])

        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME can be used in CHECK constraint.
g.test_datetime_12 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, dt DATETIME,
                      CONSTRAINT ck CHECK(
                      CAST(dt as STRING) != '2001-01-01T01:00:00Z'));]])

        local sql = [[INSERT INTO t SELECT * from t1 LIMIT 1;]]
        local res = [[Check constraint 'CK' failed for tuple]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])

        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME can be used in UNIQUE constraint.
g.test_datetime_13 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, dt DATETIME UNIQUE);]])
        box.execute([[INSERT INTO t SELECT 1, dt from t1 LIMIT 1;]])

        local sql = [[INSERT INTO t SELECT 2, dt from t1 LIMIT 1;]]
        local res = [[Duplicate key exists in unique index ]]..
                    [["unique_unnamed_T_2" in space "T" with old tuple - ]]..
                    [[[1, 2001-01-01T01:00:00Z] and new tuple - ]]..
                    "[2, 2001-01-01T01:00:00Z]"
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])

        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME can be used in Lua user-defined functions.
g.test_datetime_14_1 = function()
    g.server:exec(function()
        local body = 'function(x) return type(x) end'
        local func = {body = body, returns = 'string', param_list = {'any'},
                      exports = {'SQL'}}
        box.schema.func.create('RETURN_TYPE', func);

        local sql = [[SELECT RETURN_TYPE(dt) FROM t2;]]
        local res = {{'cdata'}, {'cdata'}, {'cdata'}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('RETURN_TYPE');

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_14_2 = function()
    g.server:exec(function()
        local body = [[
        function(x)
            local dt = require("datetime")
            return dt.new({year = 2001, month = 1, day = 1, hour = 1})
        end]]
        local func = {returns = 'datetime', exports = {'SQL'}, body = body}
        box.schema.func.create('RETURN_DATETIME', func);

        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local sql = [[SELECT RETURN_DATETIME();]]
        local res = {{dt1}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('RETURN_DATETIME');

        t.assert_equals(rows, res)
    end)
end

-- Make sure that DATETIME works properly with built-in functions.
g.test_datetime_15_1 = function()
    g.server:exec(function()
        local sql = [[SELECT ABS(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function ABS()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_2 = function()
    g.server:exec(function()
        local sql = [[SELECT AVG(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function AVG()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_3 = function()
    g.server:exec(function()
        local sql = [[SELECT CHAR(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function CHAR()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_4 = function()
    g.server:exec(function()
        local sql = [[SELECT CHARACTER_LENGTH(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function CHARACTER_LENGTH()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_5 = function()
    g.server:exec(function()
        local sql = [[SELECT CHAR_LENGTH(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function CHAR_LENGTH()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_6 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT COALESCE(NULL, dt, NULL, NULL) from t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_7 = function()
    g.server:exec(function()
        local sql = [[SELECT COUNT(dt) from t2;]]
        local res = {{3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_8 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT GREATEST(dt,
                      (SELECT dt FROM t2 LIMIT 1 OFFSET 1)) from t2;]]
        local res = {{dt2}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_9 = function()
    g.server:exec(function()
        local sql = [[SELECT GROUP_CONCAT(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function GROUP_CONCAT()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_10 = function()
    g.server:exec(function()
        local sql = [[SELECT HEX(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function HEX()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_11 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT IFNULL(dt, NULL) from t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_12 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})

        local sql =
            [[SELECT LEAST(dt, (SELECT dt FROM t2 LIMIT 1 OFFSET 1)) from t2;]]
        local res = {{dt1}, {dt2}, {dt2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_13 = function()
    g.server:exec(function()
        local sql = [[SELECT LENGTH(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LENGTH()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_14 = function()
    g.server:exec(function()
        local sql = [[SELECT dt LIKE 'a' from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LIKE()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_15 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT LIKELIHOOD(dt, 0.5e0) from t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_16 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT LIKELY(dt) from t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_17 = function()
    g.server:exec(function()
        local sql = [[SELECT LOWER(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LOWER()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_18 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT MAX(dt) from t2;]]
        local res = {{dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_19 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})

        local sql = [[SELECT MIN(dt) from t2;]]
        local res = {{dt1}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_20 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT NULLIF(dt, 1) from t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_21 = function()
    g.server:exec(function()
        local sql = [[SELECT POSITION(dt, '1') from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function POSITION()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_22 = function()
    g.server:exec(function()
        local sql = [[SELECT RANDOMBLOB(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function RANDOMBLOB()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_23 = function()
    g.server:exec(function()
        local sql = [[SELECT REPLACE(dt, '1', '2') from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function REPLACE()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_24 = function()
    g.server:exec(function()
        local sql = [[SELECT ROUND(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function ROUND()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_25 = function()
    g.server:exec(function()
        local sql = [[SELECT SOUNDEX(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function SOUNDEX()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_26 = function()
    g.server:exec(function()
        local sql = [[SELECT SUBSTR(dt, 3, 3) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function SUBSTR()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_27 = function()
    g.server:exec(function()
        local sql = [[SELECT SUM(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function SUM()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_28 = function()
    g.server:exec(function()
        local sql = [[SELECT TOTAL(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function TOTAL()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_29 = function()
    g.server:exec(function()
        local sql = [[SELECT TRIM(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function TRIM()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_30 = function()
    g.server:exec(function()
        local sql = [[SELECT TYPEOF(dt) from t2;]]
        local res = {{'datetime'}, {'datetime'}, {'datetime'}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_31 = function()
    g.server:exec(function()
        local sql = [[SELECT UNICODE(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function UNICODE()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_16 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})

        local sql = [[SELECT UNLIKELY(dt) from t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_datetime_15_33 = function()
    g.server:exec(function()
        local sql = [[SELECT UPPER(dt) from t2;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function UPPER()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_15_34 = function()
    g.server:exec(function()
        local sql = [[SELECT dt || dt from t2;]]
        local res = [[Inconsistent types: expected string or varbinary got ]]..
                    [[datetime(2001-01-01T01:00:00Z)]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME can be used in C user-defined functions.
g.test_datetime_16_1 = function()
    g.server:exec(function()
        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/sql-luatest/?.so;'..
                        build_path..'/test/sql-luatest/?.dylib;'..package.cpath
        local func = {language = 'C', returns = 'boolean', param_list = {'any'},
                      exports = {'SQL'}}
        box.schema.func.create('sql_datetime.is_datetime', func);

        local sql = [[SELECT "sql_datetime.is_datetime"(dt) FROM t2;]]
        local res = {{true}, {true}, {true}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('sql_datetime.is_datetime');

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_16_2 = function()
    g.server:exec(function()
        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/sql-luatest/?.so;'..
                        build_path..'/test/sql-luatest/?.dylib;'..package.cpath
        local func = {language = 'C', returns = 'datetime',
                      param_list = {'datetime'}, exports = {'SQL'}}
        box.schema.func.create('sql_datetime.ret_datetime', func);

        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT "sql_datetime.ret_datetime"(dt) FROM t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('sql_datetime.ret_datetime');

        t.assert_equals(rows, res)
    end)
end

-- Make sure that explicit cast from DATETIME works as intended.
g.test_datetime_17_1 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS UNSIGNED) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS INTEGER) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to integer]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_3 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS STRING) FROM t2;]]
        local res = {{"2001-01-01T01:00:00Z"}, {"2002-02-02T02:00:00Z"},
                     {"2003-03-03T03:00:00Z"}}
        local rows = box.execute(sql).rows
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_17_4 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS NUMBER) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to number]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_5 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS DOUBLE) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_6 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS BOOLEAN) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_7 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS VARBINARY) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to varbinary]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_8 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT CAST(dt AS SCALAR) FROM t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_17_9 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(dt AS UUID) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to uuid]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_17_10 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT CAST(dt AS DATETIME) FROM t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

-- Make sure that explicit cast to DATETIME works as intended.
g.test_datetime_18_1 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(1 AS DATETIME);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[integer(1) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_18_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST('1' AS DATETIME);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('1') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_18_3 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local sql = [[SELECT CAST('2001-01-01T01:00:00Z' AS DATETIME);]]
        local res = {{dt1}}
        local rows = box.execute(sql).rows
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_18_4 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(CAST(1 AS NUMBER) AS DATETIME);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[number(1) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_18_5 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(1e0 AS DATETIME);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[double(1.0) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_18_6 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(true AS DATETIME);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[boolean(TRUE) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_18_7 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(x'33' AS DATETIME);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[varbinary(x'33') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_18_8 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT CAST(CAST(dt AS SCALAR) AS DATETIME) FROM t2;]]
        local res = {{dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_18_9 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(
                      CAST('11111111-1111-1111-1111-111111111111' AS UUID)
                      AS DATETIME) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[uuid(11111111-1111-1111-1111-111111111111) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that implicit cast from DATETIME works as intended.
g.test_datetime_19_1 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d UNSIGNED PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_2 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d INTEGER PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to integer]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_3 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d STRING PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to string]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_4 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d NUMBER PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to number]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_5 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d DOUBLE PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to double]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_6 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d BOOLEAN PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to boolean]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_7 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d VARBINARY PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to varbinary]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_19_8 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d SCALAR PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = {row_count = 3}
        local rows = box.execute(sql)
        box.execute([[DROP TABLE t;]])

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_19_9 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d UUID PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to uuid]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that implicit cast to DATETIME works as intended.
g.test_datetime_20_1 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES(1);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[integer(1) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_2 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES('1');]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('1') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_3 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES('2001-01-01T01:00:00Z');]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('2001-01-01T01:00:00Z') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_4 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES(CAST(1 AS NUMBER));]]
        local res = [[Type mismatch: can not convert ]]..
                    [[number(1) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_5 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES(1e0);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[double(1.0) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_6 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES(true);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[boolean(TRUE) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_7 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES(x'33');]]
        local res = [[Type mismatch: can not convert ]]..
                    [[varbinary(x'33') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_8 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 SELECT CAST(dt AS SCALAR) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[scalar(2001-01-01T01:00:00Z) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_20_9 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t2 VALUES(
                      CAST('11111111-1111-1111-1111-111111111111' AS UUID));]]
        local res = [[Type mismatch: can not convert ]]..
                    [[uuid(11111111-1111-1111-1111-111111111111) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME can be used in trigger.
g.test_datetime_21 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE ta (i INT PRIMARY KEY, dt DATETIME);]])
        box.execute([[CREATE TABLE tb (dt DATETIME PRIMARY KEY);]])
        box.execute([[CREATE TRIGGER tr AFTER INSERT ON ta
                      FOR EACH ROW BEGIN INSERT INTO tb SELECT new.dt; END;]])

        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        box.execute([[INSERT INTO ta SELECT * FROM t1 LIMIT 3;]])
        local sql = [[SELECT * FROM tb;]]
        local res = {{dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows

        box.execute([[DROP TABLE ta;]])
        box.execute([[DROP TABLE tb;]])

        t.assert_equals(rows, res)
    end)
end

-- Make sure that DATETIME can work with JOINs.
g.test_datetime_22_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT * FROM t1 JOIN t2 on t1.dt = t2.dt;]]
        local res = {{1, dt1, dt1}, {2, dt2, dt2}, {3, dt3, dt3},
                     {4, dt1, dt1}, {5, dt2, dt2}, {6, dt3, dt3}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_22_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT * FROM t1 LEFT JOIN t2 on t1.dt = t2.dt;]]
        local res = {{1, dt1, dt1}, {2, dt2, dt2}, {3, dt3, dt3},
                     {4, dt1, dt1}, {5, dt2, dt2}, {6, dt3, dt3}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_22_3 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        local sql = [[SELECT * FROM t1 INNER JOIN t2 on t1.dt = t2.dt;]]
        local res = {{1, dt1, dt1}, {2, dt2, dt2}, {3, dt3, dt3},
                     {4, dt1, dt1}, {5, dt2, dt2}, {6, dt3, dt3}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

-- Make sure that numeric arithmetic operations work with DATETIME as intended.
g.test_datetime_23_1 = function()
    g.server:exec(function()
        local sql = [[SELECT -dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) ]]..
                    [[to integer, decimal or double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_23_2 = function()
    g.server:exec(function()
        local sql = [[SELECT dt + 1 FROM t2;]]
        local res = [[Type mismatch: can not convert integer(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_23_3 = function()
    g.server:exec(function()
        local sql = [[SELECT dt - 1 FROM t2;]]
        local res = [[Type mismatch: can not convert integer(1) to ]]..
                    [[datetime or interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_23_4 = function()
    g.server:exec(function()
        local sql = [[SELECT dt / 2 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) ]]..
                    [[to integer, decimal or double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_23_5 = function()
    g.server:exec(function()
        local sql = [[SELECT dt * 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) ]]..
                    [[to integer, decimal or double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_23_6 = function()
    g.server:exec(function()
        local sql = [[SELECT dt % 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to integer]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that bitwise operations work with DATETIME as intended.
g.test_datetime_24_1 = function()
    g.server:exec(function()
        local sql = [[SELECT ~dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_24_2 = function()
    g.server:exec(function()
        local sql = [[SELECT dt >> 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_24_3 = function()
    g.server:exec(function()
        local sql = [[SELECT dt << 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_24_4 = function()
    g.server:exec(function()
        local sql = [[SELECT dt | 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_24_5 = function()
    g.server:exec(function()
        local sql = [[SELECT dt & 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that logical operations work with DATETIME as intended.
g.test_datetime_25_1 = function()
    g.server:exec(function()
        local sql = [[SELECT NOT dt FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_25_2 = function()
    g.server:exec(function()
        local sql = [[SELECT dt AND true FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_25_3 = function()
    g.server:exec(function()
        local sql = [[SELECT dt OR true FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that comparison work with DATETIME as intended.
g.test_datetime_26_1 = function()
    g.server:exec(function()
        local sql = [[SELECT dt > 1 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[integer(1) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_2 = function()
    g.server:exec(function()
        local sql = [[SELECT dt < '1' FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('1') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_3 = function()
    g.server:exec(function()
        local sql = [[SELECT dt == '2001-01-01T01:00:00Z' FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('2001-01-01T01:00:00Z') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_4 = function()
    g.server:exec(function()
        local sql = [[SELECT dt >= CAST(1 AS NUMBER) FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[number(1) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_5 = function()
    g.server:exec(function()
        local sql = [[SELECT dt <= 1e0 FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[double(1.0) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_6 = function()
    g.server:exec(function()
        local sql = [[SELECT dt > true FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[boolean(TRUE) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_7 = function()
    g.server:exec(function()
        local sql = [[SELECT dt < x'33' FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[varbinary(x'33') to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_datetime_26_8 = function()
    g.server:exec(function()
        local sql = [[SELECT dt == CAST(dt AS SCALAR) FROM t2;]]
        local res = {{true}, {true}, {true}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_26_9 = function()
    g.server:exec(function()
        local sql = [[SELECT dt >
                      CAST('11111111-1111-1111-1111-111111111111' AS UUID)
                      FROM t2;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[uuid(11111111-1111-1111-1111-111111111111) to datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that SCALAR primary key can contain DATETIME.
g.test_datetime_27 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local dt3 = dt.new({year = 2003, month = 3, day = 3, hour = 3})
        box.execute([[CREATE TABLE t (s SCALAR PRIMARY KEY);]])
        box.execute([[INSERT INTO t VALUES(1), (true), ('asd')]])
        box.execute([[INSERT INTO t SELECT dt FROM t2;]])
        local sql = [[SELECT * FROM t;]]
        local res = {{true}, {1}, {'asd'}, {dt1}, {dt2}, {dt3}}
        local rows = box.execute(sql).rows
        box.execute([[DROP TABLE t;]])

        t.assert_equals(rows, res)
    end)
end

-- Make sure that DATETIME value can be bound.
g.test_datetime_28_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt4 = dt.new({year = 2004, month = 4, day = 4, hour = 4})
        local sql = [[SELECT ?;]]
        local res = {{dt4}}
        local rows = box.execute(sql, {dt4}).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_28_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt4 = dt.new({year = 2004, month = 4, day = 4, hour = 4})
        local sql = [[SELECT $1;]]
        local res = {{dt4}}
        local rows = box.execute(sql, {dt4}).rows

        t.assert_equals(rows, res)
    end)
end

-- Make sure that function NOW() works as intended.
g.test_datetime_29_1 = function()
    g.server:exec(function()
        local sql = [[SELECT typeof(now());]]
        local res = {{"datetime"}}
        local rows = box.execute(sql).rows

        t.assert_equals(rows, res)
    end)
end

g.test_datetime_29_2 = function()
    g.server:exec(function()
        local sql = [[SELECT now(), now() FROM (values(1), (2), (3));]]
        local rows = box.execute(sql).rows

        t.assert_equals(rows[1][1], rows[1][2])
        t.assert_equals(rows[1][1], rows[2][1])
        t.assert_equals(rows[1][1], rows[2][2])
        t.assert_equals(rows[1][1], rows[3][1])
        t.assert_equals(rows[1][1], rows[3][2])
    end)
end

-- Make sure that function DATE_PART() works as intended.
g.test_datetime_30_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('millennium', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{2}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 2001})}).rows
        res = {{3}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1999})}).rows
        res = {{2}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 0})}).rows
        res = {{-1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -999})}).rows
        res = {{-1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1000})}).rows
        res = {{-2}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1001})}).rows
        res = {{-2}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('century', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{20}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 2001})}).rows
        res = {{21}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1999})}).rows
        res = {{20}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 0})}).rows
        res = {{-1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -999})}).rows
        res = {{-10}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1000})}).rows
        res = {{-11}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1001})}).rows
        res = {{-11}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_3 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('decade', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{200}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 2001})}).rows
        res = {{200}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1999})}).rows
        res = {{199}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 0})}).rows
        res = {{-1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -999})}).rows
        res = {{-100}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1000})}).rows
        res = {{-101}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1001})}).rows
        res = {{-101}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_4 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('year', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{2000}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 2001})}).rows
        res = {{2001}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1999})}).rows
        res = {{1999}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 0})}).rows
        res = {{0}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -999})}).rows
        res = {{-999}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1000})}).rows
        res = {{-1000}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = -1001})}).rows
        res = {{-1001}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_5 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('quarter', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{2}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 1, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 3, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 4, day = 1})}).rows
        res = {{2}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 3, day = 31})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 7, day = 15})}).rows
        res = {{3}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 12, day = 31})}).rows
        res = {{4}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_6 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('month', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{4}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 3})}).rows
        res = {{3}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 4})}).rows
        res = {{4}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 3})}).rows
        res = {{3}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 7})}).rows
        res = {{7}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 12})}).rows
        res = {{12}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_7 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('week', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{14}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 1, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 3, day = 1})}).rows
        res = {{9}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 4, day = 1})}).rows
        res = {{13}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 3, day = 31})}).rows
        res = {{13}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 7, day = 15})}).rows
        res = {{28}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 12, day = 31})}).rows
        res = {{1}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_8 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('day', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{5}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 1, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 3, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 4, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 3, day = 31})}).rows
        res = {{31}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 7, day = 15})}).rows
        res = {{15}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({month = 12, day = 31})}).rows
        res = {{31}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_9 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('dow', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{3}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 1, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 3, day = 1})}).rows
        res = {{4}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 4, day = 1})}).rows
        res = {{7}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 3, day = 31})}).rows
        res = {{6}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 7, day = 15})}).rows
        res = {{7}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 12, day = 31})}).rows
        res = {{1}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_10 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('doy', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{96}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 1, day = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 3, day = 1})}).rows
        res = {{60}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 4, day = 1})}).rows
        res = {{91}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 3, day = 31})}).rows
        res = {{90}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 7, day = 15})}).rows
        res = {{196}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({year = 1, month = 12, day = 31})}).rows
        res = {{365}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_11 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('hour', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{6}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({hour = 13, min = 1})}).rows
        res = {{13}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({hour = 1, min = 20})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({hour = 7, min = 47})}).rows
        res = {{7}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({hour = 23, min = 59})}).rows
        res = {{23}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_12 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('minute', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{33}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 1, sec = 13})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 20, sec = 59})}).rows
        res = {{20}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 47, sec = 1})}).rows
        res = {{47}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 59, sec = 29})}).rows
        res = {{59}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_13 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('second', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{22}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 1, sec = 13})}).rows
        res = {{13}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 20, sec = 59})}).rows
        res = {{59}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 47, sec = 1})}).rows
        res = {{1}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({min = 59, sec = 29})}).rows
        res = {{29}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_14 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('millisecond', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{523}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 1, nsec = 13123546})}).rows
        res = {{13}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 20, nsec = 591256})}).rows
        res = {{0}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 47, nsec = 176318368})}).rows
        res = {{176}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 59, nsec = 29})}).rows
        res = {{0}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_15 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('microsecond', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{523999}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 1, nsec = 13123546})}).rows
        res = {{13123}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 20, nsec = 591256})}).rows
        res = {{591}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 47, nsec = 176318368})}).rows
        res = {{176318}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 59, nsec = 29})}).rows
        res = {{0}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_16 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('nanosecond', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{523999111}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 1, nsec = 13123546})}).rows
        res = {{13123546}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 20, nsec = 591256})}).rows
        res = {{591256}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 47, nsec = 176318368})}).rows
        res = {{176318368}}
        t.assert_equals(rows, res)

        rows = box.execute(sql, {dt.new({sec = 59, nsec = 29})}).rows
        res = {{29}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_17 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('epoch', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{954916402}}
        t.assert_equals(rows, res)

        dt0 = dt.new({year = 1, month = 1, day = 15, hour = 6, min = 12})
        rows = box.execute(sql, {dt0}).rows
        res = {{-62134364880}}
        t.assert_equals(rows, res)

        dt0 = dt.new({year = -1000, month = 11, day = 1, hour = 16, sec = 41})
        rows = box.execute(sql, {dt0}).rows
        res = {{-93697804759}}
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_30_18 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt0 = dt.new({year = 2000, month = 4, day = 5, hour = 6,
                            min = 33, sec = 22, nsec = 523999111})

        local sql = [[SELECT date_part('timezone_offset', ?);]]
        local rows = box.execute(sql, {dt0}).rows
        local res = {{0}}
        t.assert_equals(rows, res)

        dt0 = dt.new({year = 1, month = 1, day = 15, hour = 6, min = 12})
        rows = box.execute(sql, {dt0}).rows
        res = {{0}}
        t.assert_equals(rows, res)

        dt0 = dt.new({year = -1000, month = 11, day = 1, hour = 16, sec = 41})
        rows = box.execute(sql, {dt0}).rows
        res = {{0}}
        t.assert_equals(rows, res)

        dt0 = dt.new({year = 1000000, sec = 23})
        rows = box.execute(sql, {dt0}).rows
        res = {{0}}
        t.assert_equals(rows, res)

        dt0 = dt.new({year = -1000000})
        rows = box.execute(sql, {dt0}).rows
        res = {{0}}
        t.assert_equals(rows, res)
    end)
end

-- Make sure that arithmetic for datetime works as intended.
g.test_datetime_31_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local itv = dt.interval
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        t.assert_equals(dt2 - dt1, itv1)

        local rows = box.execute([[SELECT $1 - $2;]], {dt2, dt1}).rows
        t.assert_equals(rows, {{itv1}})
    end)
end

g.test_datetime_31_2 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local _, err = box.execute([[SELECT $1 + $2;]], {dt1, dt2})
        t.assert_equals(err.message, [[Type mismatch: can not convert ]]..
                                     [[datetime(2002-02-02T02:00:00Z) to ]]..
                                     [[interval]])
    end)
end

g.test_datetime_31_3 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local itv = dt.interval
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        local rows = box.execute([[SELECT $1 + $2;]], {dt1, itv1}).rows
        t.assert_equals(rows, {{dt2}})
    end)
end

g.test_datetime_31_4 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local itv = dt.interval
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        local rows = box.execute([[SELECT $1 - $2;]], {dt2, itv1}).rows
        t.assert_equals(rows, {{dt1}})
    end)
end

g.test_datetime_31_5 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local itv = dt.interval
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local dt2 = dt.new({year = 2002, month = 2, day = 2, hour = 2})
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        local rows = box.execute([[SELECT $1 + $2;]], {itv1, dt1}).rows
        t.assert_equals(rows, {{dt2}})
    end)
end

g.test_datetime_31_6 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local itv = dt.interval
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        local _, err = box.execute([[SELECT $1 - $2;]], {itv1, dt1})
        t.assert_equals(err.message, [[Type mismatch: can not convert ]]..
                                     [[datetime(2001-01-01T01:00:00Z) to ]]..
                                     [[interval]])
    end)
end

g.test_datetime_31_7 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        local itv2 = itv.new({year = 2, month = 3, day = 4, hour = 5})
        local itv3 = itv.new({year = 3, month = 4, day = 5, hour = 6})
        local rows = box.execute([[SELECT $1 - $2;]], {itv3, itv1}).rows
        t.assert_equals(rows, {{itv2}})
    end)
end

g.test_datetime_31_8 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 1, day = 1, hour = 1})
        local itv2 = itv.new({year = 2, month = 3, day = 4, hour = 5})
        local itv3 = itv.new({year = 3, month = 4, day = 5, hour = 6})
        local rows = box.execute([[SELECT $1 + $2;]], {itv2, itv1}).rows
        t.assert_equals(rows, {{itv3}})
    end)
end

-- Make sure cast from STRING to DATETIME works as intended.
g.test_datetime_32_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2000, month = 2, day = 29, hour = 1})
        local sql = [[SELECT CAST('2000-02-29T01:00:00Z' AS DATETIME);]]
        local res = {{dt1}}
        local rows = box.execute(sql).rows
        t.assert_equals(rows, res)
    end)
end

g.test_datetime_32_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST('2001-02-29T01:00:00Z' AS DATETIME);]]
        local _, err = box.execute(sql)
        local res = [[Type mismatch: can not convert ]]..
                    [[string('2001-02-29T01:00:00Z') to datetime]]
        t.assert_equals(err.message, res)
    end)
end

-- Make sure cast from MAP to DATETIME works as intended.

--
-- The result of CAST() from MAP value to DATETIME must be equal to the result
-- of calling require('datetime').new() with the corresponding table as an
-- argument.
--
g.test_datetime_33_1 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local v = setmetatable({}, { __serialize = 'map' })
        local sql = [[SELECT CAST(#v AS DATETIME);]]
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.something = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v = {year = 1, month = 1, day = 1, hour = 1, min = 1, sec = 1}
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.nsec = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.nsec = nil
        v.usec = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.usec = nil
        v.msec = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v = {year = 1.1, month = 1.1, day = 1.1, hour = 1.1, min = 1.1,
             sec = 1.1}
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v = {timestamp = 1}
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.nsec = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.nsec = nil
        v.usec = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v.usec = nil
        v.msec = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})

        v = {timestamp = 1.5}
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{dt.new(v)}})
    end)
end

--
-- Make sure an error is thrown if the DATETIME value cannot be constructed from
-- the corresponding table.
--
g.test_datetime_33_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(#v AS DATETIME);]]

        -- "year" cannot be more than 5879611.
        local v = {year = 5879612}
        local _, err = box.execute(sql, {{['#v'] = v}})
        local res = [[Type mismatch: can not convert ]]..
                    [[map({"year": 5879612}) to datetime]]
        t.assert_equals(err.message, res)

        -- "year" cannot be less than -5879610.
        v = {year = -5879611}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"year": -5879611}) to datetime]]
        t.assert_equals(err.message, res)

        -- "month" cannot be more than 12.
        v = {month = 13}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"month": 13}) to datetime]]
        t.assert_equals(err.message, res)

        -- "month" cannot be less than 1.
        v = {month = 0}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"month": 0}) to datetime]]
        t.assert_equals(err.message, res)

        -- "day" cannot be more than 31.
        v = {day = 32}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"day": 32}) to datetime]]
        t.assert_equals(err.message, res)

        -- "day" cannot be less than 1.
        v = {day = 0}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"day": 0}) to datetime]]
        t.assert_equals(err.message, res)

        -- "hour" cannot be more than 23.
        v = {hour = 24}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"hour": 24}) to datetime]]
        t.assert_equals(err.message, res)

        -- "hour" cannot be less than 0.
        v = {hour = -1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"hour": -1}) to datetime]]
        t.assert_equals(err.message, res)

        -- "min" cannot be more than 59.
        v = {min = 60}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"min": 60}) to datetime]]
        t.assert_equals(err.message, res)

        -- "min" cannot be less than 0.
        v = {min = -1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"min": -1}) to datetime]]
        t.assert_equals(err.message, res)

        -- "sec" cannot be more than 60.
        v = {sec = 61}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"sec": 61}) to datetime]]
        t.assert_equals(err.message, res)

        -- "sec" cannot be less than 0.
        v = {sec = -1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"sec": -1}) to datetime]]
        t.assert_equals(err.message, res)

        -- "msec" cannot be more than 1000.
        v = {msec = 2000}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"msec": 2000}) to datetime]]
        t.assert_equals(err.message, res)

        -- "msec" cannot be less than 0.
        v = {msec = -1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"msec": -1}) to datetime]]
        t.assert_equals(err.message, res)

        -- "usec" cannot be more than 1000000.
        v = {usec = 2000000}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"usec": 2000000}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        -- "usec" cannot be less than 0.
        v = {usec = -1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"usec": -1}) to datetime]]
        t.assert_equals(err.message, res)

        -- "nsec" cannot be more than 1000000000.
        v = {nsec = 2000000000}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"nsec": 2000000000}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        -- "nsec" cannot be less than 0.
        v = {nsec = -1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"nsec": -1}) to datetime]]
        t.assert_equals(err.message, res)

        -- "tzoffset" cannot be more than 840.
        v = {tzoffset = 841}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"tzoffset": 841}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        -- "tzoffset" cannot be less than -720.
        v = {tzoffset = -721}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"tzoffset": -721}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        -- Only one of "msec", "usec" and "nsec" can be specified.
        v = {msec = 1, usec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"usec": 1, "msec": 1}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        v = {msec = 1, nsec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"nsec": 1, "msec": 1}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        v = {nsec = 1, usec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"usec": 1, "nsec": 1}) ]]..
              [[to datetime]]
        t.assert_equals(err.message, res)

        --
        -- "timestamp" cannot be specified when any of "year", "month", "day",
        -- "hour", "min", "sec" is specified.
        --
        v = {timestamp = 1, year = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"timestamp": 1, "year": 1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1, month = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"month": 1, "timestamp": 1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1, day = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"day": 1, "timestamp": 1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1, hour = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"timestamp": 1, "hour": 1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1, min = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"min": 1, "timestamp": 1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1, sec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"timestamp": 1, "sec": 1}) to datetime]]
        t.assert_equals(err.message, res)

        --
        -- If "timestamp" contains fractional part, it cannot be when any of
        -- "msec", "usec", "nsec" is specified.
        --
        v = {timestamp = 1.1, msec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"msec": 1, "timestamp": 1.1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1.1, usec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"usec": 1, "timestamp": 1.1}) to datetime]]
        t.assert_equals(err.message, res)

        v = {timestamp = 1.1, nsec = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"nsec": 1, "timestamp": 1.1}) to datetime]]
        t.assert_equals(err.message, res)
    end)
end

--
-- Make sure that any of the DECIMAL, INTEGER, and DOUBLE values can be used as
-- values in the MAP converted to a DATETIME.
--
g.test_datetime_33_3 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 1})
        local sql = [[SELECT CAST({'year': 1.1} AS DATETIME);]]
        t.assert_equals(box.execute(sql).rows, {{dt1}})

        sql = [[SELECT CAST({'year': 1.1e0} AS DATETIME);]]
        t.assert_equals(box.execute(sql).rows, {{dt1}})

        sql = [[SELECT CAST({'year': 1} AS DATETIME);]]
        t.assert_equals(box.execute(sql).rows, {{dt1}})
    end)
end

-- Make sure cast from MAP to INTERVAL works as intended.

--
-- The result of CAST() from MAP value to INTERVAL must be equal to the result
-- of calling require('datetime').interval.new() with the corresponding table as
-- an argument.
--
g.test_datetime_34_1 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local v = setmetatable({}, { __serialize = 'map' })
        local sql = [[SELECT CAST(#v AS INTERVAL);]]
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{itv.new(v)}})

        v.something = 1
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{itv.new(v)}})

        v = {year = 1, month = 1, week = 1, day = 1, hour = 1, min = 1, sec = 1,
             nsec = 1, adjust = 'none'}
        t.assert_equals(box.execute(sql, {{['#v'] = v}}).rows, {{itv.new(v)}})
    end)
end

--
-- Make sure an error is thrown if the INTERVAL value cannot be constructed from
-- the corresponding table.
--
g.test_datetime_34_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(#v AS INTERVAL);]]

        -- "year" cannot be more than 11759221.
        local v = {year = 11759222}
        local _, err = box.execute(sql, {{['#v'] = v}})
        local res = [[Type mismatch: can not convert ]]..
                    [[map({"year": 11759222}) to interval]]
        t.assert_equals(err.message, res)

        -- "year" cannot be less than -11759221.
        v = {year = -11759222}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"year": -11759222}) to interval]]
        t.assert_equals(err.message, res)

        -- "month" cannot be more than 141110652.
        v = {month = 141110653}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"month": 141110653}) to interval]]
        t.assert_equals(err.message, res)

        -- "month" cannot be less than -141110652.
        v = {month = -141110653}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"month": -141110653}) to interval]]
        t.assert_equals(err.message, res)

        -- "week" cannot be more than 613579352.
        v = {week = 613579353}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"week": 613579353}) to interval]]
        t.assert_equals(err.message, res)

        -- "week" cannot be less than -613579352.
        v = {week = -613579353}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"week": -613579353}) to interval]]
        t.assert_equals(err.message, res)

        -- "day" cannot be more than 4295055470.
        v = {day = 4295055471}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"day": 4295055471}) to interval]]
        t.assert_equals(err.message, res)

        -- "day" cannot be less than -4295055470.
        v = {day = -4295055471}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"day": -4295055471}) to interval]]
        t.assert_equals(err.message, res)

        -- "hour" cannot be more than 103081331286.
        v = {hour = 103081331287}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"hour": 103081331287}) to interval]]
        t.assert_equals(err.message, res)

        -- "hour" cannot be less than 103081331286.
        v = {hour = -103081331287}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"hour": -103081331287}) to interval]]
        t.assert_equals(err.message, res)

        -- "min" cannot be more than 6184879877160.
        v = {min = 6184879877161}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"min": 6184879877161}) to interval]]
        t.assert_equals(err.message, res)

        -- "min" cannot be less than -6184879877160.
        v = {min = -6184879877161}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"min": -6184879877161}) to interval]]
        t.assert_equals(err.message, res)

        -- "sec" cannot be more than 371092792629600.
        v = {sec = 371092792629601}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"sec": 371092792629601}) to interval]]
        t.assert_equals(err.message, res)

        -- "sec" cannot be less than -371092792629600.
        v = {sec = -371092792629601}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert ]]..
              [[map({"sec": -371092792629601}) to interval]]
        t.assert_equals(err.message, res)

        -- "nsec" cannot be more than 2147483647.
        v = {nsec = 2147483648}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"nsec": 2147483648}) ]]..
              [[to interval]]
        t.assert_equals(err.message, res)

        -- "nsec" cannot be less than -2147483647.
        v = {nsec = -2147483648}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"nsec": -2147483648}) ]]..
              [[to interval]]
        t.assert_equals(err.message, res)

        -- "adjust" cannot be anything other than "none", "excess", "last".
        v = {adjust = 1}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"adjust": 1}) ]]..
              [[to interval]]
        t.assert_equals(err.message, res)

        v = {adjust = 'asd'}
        _, err = box.execute(sql, {{['#v'] = v}})
        res = [[Type mismatch: can not convert map({"adjust": "asd"}) ]]..
              [[to interval]]
        t.assert_equals(err.message, res)
    end)
end

--
-- Make sure that any of the DECIMAL, INTEGER, and DOUBLE values can be used as
-- values in the MAP converted to a INTERVAL.
--
g.test_datetime_34_3 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local dt1 = itv.new({year = 1})
        local sql = [[SELECT CAST({'year': 1.0} AS INTERVAL);]]
        t.assert_equals(box.execute(sql).rows, {{dt1}})

        sql = [[SELECT CAST({'year': 1.e0} AS INTERVAL);]]
        t.assert_equals(box.execute(sql).rows, {{dt1}})

        sql = [[SELECT CAST({'year': 1} AS INTERVAL);]]
        t.assert_equals(box.execute(sql).rows, {{dt1}})
    end)
end

-- Properly compute type of result of DATETIME arithmetic.
g.test_datetime_35 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 1})
        local itv1 = dt.interval.new({year = 1})

        box.execute([[CREATE TABLE t(dt DATETIME PRIMARY KEY, itv INTERVAL);]])
        box.execute([[INSERT INTO t VALUES (?, ?);]], {dt1, itv1})

        local sql = [[SELECT typeof(dt - dt) FROM t;]]
        t.assert_equals(box.execute(sql).rows, {{'interval'}})

        sql = [[SELECT dt - dt FROM t;]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'interval')

        sql = [[SELECT typeof(dt - itv) FROM t;]]
        t.assert_equals(box.execute(sql).rows, {{'datetime'}})

        sql = [[SELECT dt - itv FROM t;]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'datetime')

        sql = [[SELECT typeof(dt + itv) FROM t;]]
        t.assert_equals(box.execute(sql).rows, {{'datetime'}})

        sql = [[SELECT dt + itv FROM t;]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'datetime')

        sql = [[SELECT typeof(itv - itv) FROM t;]]
        t.assert_equals(box.execute(sql).rows, {{'interval'}})

        sql = [[SELECT itv - itv FROM t;]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'interval')

        sql = [[SELECT typeof(itv + itv) FROM t;]]
        t.assert_equals(box.execute(sql).rows, {{'interval'}})

        sql = [[SELECT itv + itv FROM t;]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'interval')
        box.execute([[DROP TABLE t;]])
    end)
end
