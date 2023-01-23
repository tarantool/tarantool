local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'interval'})
    g.server:start()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})
        local fmt1 = {{'I', 'integer'}, {'ITV', 'interval'}}
        local t0 = box.schema.space.create('T0', {format = fmt1})
        box.space.T0:create_index('I')
        t0:insert({1, itv1})
        t0:insert({2, itv2})
    end)
end)

g.after_all(function()
    g.server:exec(function()
        box.space.T0:drop()
    end)
    g.server:stop()
end)

-- Make sure that it is possible to create spaces with INTERVAL field.
g.test_interval_1 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT PRIMARY KEY, itv INTERVAL);]]
        local res = {row_count = 1}
        t.assert_equals(box.execute(sql), res)
        box.execute([[DROP TABLE t;]])
    end)
end

-- Make sure that INTERVAL cannot be used as primary key.
g.test_interval_2 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (itv INTERVAL PRIMARY KEY);]]
        local res = "Can't create or modify index 'pk_unnamed_T_1' in space "..
                    "'T': field type 'interval' is not supported"
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL can be selected.
g.test_interval_3 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT itv FROM t0;]]
        local res = {{itv1}, {itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that INTERVAL cannot be used in GROUP BY.
g.test_interval_4 = function()
    g.server:exec(function()
        local sql = [[SELECT itv FROM t0 GROUP BY itv;]]
        local res = "Type mismatch: can not convert interval(+1 years, "..
                    "2 months, 3 days, 4 hours) to comparable type"
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL can be used in subselects.
g.test_interval_5 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT * FROM (SELECT * FROM t0 LIMIT 2 OFFSET 1);]]
        local res = {{2, itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that INTERVAL cannot work with DISTINCT.
g.test_interval_6 = function()
    g.server:exec(function()
        local sql = [[SELECT DISTINCT itv FROM t0;]]
        local res = "Failed to execute SQL statement: field type 'interval' "..
                    "is not comparable"
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL can work with VIEW.
g.test_interval_7 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        box.execute([[CREATE VIEW v AS SELECT itv FROM t0;]])
        local sql = [[SELECT * FROM v;]]
        local res = {{itv1}, {itv2}}
        local rows = box.execute(sql).rows
        box.execute([[DROP VIEW v;]])

        t.assert_equals(rows, res)
    end)
end

-- Make sure that INTERVAL cannot be used in LIMIT.
g.test_interval_8 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t0 LIMIT (SELECT itv FROM t0 LIMIT 1);]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[Only positive integers are allowed in the LIMIT clause]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL cannot be used in OFFSET.
g.test_interval_9 = function()
    g.server:exec(function()
        local sql =
            [[SELECT * FROM t0 LIMIT 1 OFFSET (SELECT itv FROM t0 LIMIT 1);]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[Only positive integers are allowed in the OFFSET clause]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL can be used in CHECK constraint.
g.test_interval_10 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, itv INTERVAL,
                      CONSTRAINT ck CHECK(CAST(itv as STRING) !=
                      '+1 years, 2 months, 3 days, 4 hours'));]])

        local sql = [[INSERT INTO t SELECT * from t0 LIMIT 1;]]
        local res = [[Check constraint failed 'CK': CAST(itv as STRING) != ]]..
                    [['+1 years, 2 months, 3 days, 4 hours']]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])

        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL cannot be used in UNIQUE constraint.
g.test_interval_11 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT PRIMARY KEY, itv INTERVAL UNIQUE);]]
        local res = [[Can't create or modify index 'unique_unnamed_T_2' in ]]..
                    [[space 'T': field type 'interval' is not supported]]
        local _, err = box.execute(sql)

        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL can be used in Lua user-defined functions.
g.test_interval_12_1 = function()
    g.server:exec(function()
        local body = 'function(x) return type(x) end'
        local func = {body = body, returns = 'string', param_list = {'any'},
                      exports = {'SQL'}}
        box.schema.func.create('RETURN_TYPE', func);

        local sql = [[SELECT RETURN_TYPE(itv) FROM t0;]]
        local res = {{'cdata'}, {'cdata'}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('RETURN_TYPE');

        t.assert_equals(rows, res)
    end)
end

g.test_interval_12_2 = function()
    g.server:exec(function()
        local body = [[
        function(x)
            local itv = require("datetime").interval
            return itv.new({year = 1, month = 2, day = 3, hour = 4})
        end]]
        local func = {returns = 'interval', exports = {'SQL'}, body = body}
        box.schema.func.create('RETURN_INTERVAL', func);

        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local sql = [[SELECT RETURN_INTERVAL();]]
        local res = {{itv1}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('RETURN_INTERVAL');

        t.assert_equals(rows, res)
    end)
end

-- Make sure that INTERVAL can be used in C user-defined functions.
g.test_interval_13_1 = function()
    g.server:exec(function()
        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/sql-luatest/?.so;'..
                        build_path..'/test/sql-luatest/?.dylib;'..package.cpath
        local func = {language = 'C', returns = 'boolean', param_list = {'any'},
                      exports = {'SQL'}}
        box.schema.func.create('sql_interval.is_interval', func);

        local sql = [[SELECT "sql_interval.is_interval"(itv) FROM t0;]]
        local res = {{true}, {true}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('sql_interval.is_interval');

        t.assert_equals(rows, res)
    end)
end

g.test_interval_13_2 = function()
    g.server:exec(function()
        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/sql-luatest/?.so;'..
                        build_path..'/test/sql-luatest/?.dylib;'..package.cpath
        local func = {language = 'C', returns = 'interval',
                      param_list = {'interval'}, exports = {'SQL'}}
        box.schema.func.create('sql_interval.ret_interval', func);

        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})
        local sql = [[SELECT "sql_interval.ret_interval"(itv) FROM t0;]]
        local res = {{itv1}, {itv2}}
        local rows = box.execute(sql).rows
        box.schema.func.drop('sql_interval.ret_interval');

        t.assert_equals(rows, res)
    end)
end

-- Make sure that INTERVAL works properly with built-in functions.
g.test_interval_14_1 = function()
    g.server:exec(function()
        local sql = [[SELECT ABS(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function ABS()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_2 = function()
    g.server:exec(function()
        local sql = [[SELECT AVG(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function AVG()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_3 = function()
    g.server:exec(function()
        local sql = [[SELECT CHAR(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function CHAR()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_4 = function()
    g.server:exec(function()
        local sql = [[SELECT CHARACTER_LENGTH(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function CHARACTER_LENGTH()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_5 = function()
    g.server:exec(function()
        local sql = [[SELECT CHAR_LENGTH(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function CHAR_LENGTH()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_6 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT COALESCE(NULL, itv, NULL, NULL) FROM t0;]]
        local res = {{itv1}, {itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_7 = function()
    g.server:exec(function()
        local sql = [[SELECT COUNT(itv) FROM t0;]]
        local res = {{2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_8 = function()
    g.server:exec(function()
        local sql = [[SELECT GREATEST(itv, itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function GREATEST()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_9 = function()
    g.server:exec(function()
        local sql = [[SELECT GROUP_CONCAT(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function GROUP_CONCAT()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_10 = function()
    g.server:exec(function()
        local sql = [[SELECT HEX(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function HEX()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_11 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT IFNULL(itv, NULL) FROM t0;]]
        local res = {{itv1}, {itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_12 = function()
    g.server:exec(function()
        local sql = [[SELECT LEAST(itv, itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LEAST()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_13 = function()
    g.server:exec(function()
        local sql = [[SELECT LENGTH(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LENGTH()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_14 = function()
    g.server:exec(function()
        local sql = [[SELECT itv LIKE 'a' FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LIKE()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_15 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT LIKELIHOOD(itv, 0.5e0) FROM t0;]]
        local res = {{itv1}, {itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_16 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT LIKELY(itv) FROM t0;]]
        local res = {{itv1}, {itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_17 = function()
    g.server:exec(function()
        local sql = [[SELECT LOWER(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function LOWER()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_18 = function()
    g.server:exec(function()
        local sql = [[SELECT MAX(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function MAX()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_19 = function()
    g.server:exec(function()
        local sql = [[SELECT MIN(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function MIN()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_20 = function()
    g.server:exec(function()
        local sql = [[SELECT NULLIF(itv, '1') FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function NULLIF()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_21 = function()
    g.server:exec(function()
        local sql = [[SELECT POSITION(itv, '1') FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function POSITION()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_22 = function()
    g.server:exec(function()
        local sql = [[SELECT RANDOMBLOB(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function RANDOMBLOB()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_23 = function()
    g.server:exec(function()
        local sql = [[SELECT REPLACE(itv, '1', '2') FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function REPLACE()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_24 = function()
    g.server:exec(function()
        local sql = [[SELECT ROUND(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function ROUND()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_25 = function()
    g.server:exec(function()
        local sql = [[SELECT SOUNDEX(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function SOUNDEX()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_26 = function()
    g.server:exec(function()
        local sql = [[SELECT SUBSTR(itv, 3, 3) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function SUBSTR()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_27 = function()
    g.server:exec(function()
        local sql = [[SELECT SUM(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function SUM()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_28 = function()
    g.server:exec(function()
        local sql = [[SELECT TOTAL(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function TOTAL()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_29 = function()
    g.server:exec(function()
        local sql = [[SELECT TRIM(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function TRIM()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_30 = function()
    g.server:exec(function()
        local sql = [[SELECT TYPEOF(itv) FROM t0;]]
        local res = {{'interval'}, {'interval'}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_31 = function()
    g.server:exec(function()
        local sql = [[SELECT UNICODE(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function UNICODE()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_32 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        local sql = [[SELECT UNLIKELY(itv) FROM t0;]]
        local res = {{itv1}, {itv2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_interval_14_33 = function()
    g.server:exec(function()
        local sql = [[SELECT UPPER(itv) FROM t0;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[wrong arguments for function UPPER()]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_14_34 = function()
    g.server:exec(function()
        local sql = [[SELECT itv || itv FROM t0;]]
        local res = [[Inconsistent types: expected string or varbinary got ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours)]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that explicit cast from INTERVAL works as intended.
g.test_interval_17_1 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS UNSIGNED) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS INTEGER) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[integer]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_3 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS STRING) FROM t0;]]
        local res = {{'+1 years, 2 months, 3 days, 4 hours'},
                     {'+5 minutes, 6 seconds, 7 nanoseconds'}}
        local rows = box.execute(sql).rows
        t.assert_equals(rows, res)
    end)
end

g.test_interval_17_4 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS NUMBER) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[number]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_5 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS DOUBLE) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_6 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS BOOLEAN) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_7 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS VARBINARY) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[varbinary]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_8 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS SCALAR) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[scalar]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_9 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS UUID) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[uuid]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_10 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS DATETIME) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[datetime]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_17_11 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS ANY) FROM t0;]]
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})
        local res = {{itv1}, {itv2}}
        local rows = box.execute(sql).rows
        t.assert_equals(rows, res)
    end)
end

g.test_interval_17_12 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(itv AS INTERVAL) FROM t0;]]
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})
        local res = {{itv1}, {itv2}}
        local rows = box.execute(sql).rows
        t.assert_equals(rows, res)
    end)
end

-- Make sure that explicit cast to INTERVAL works as intended.
g.test_interval_18_1 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(1 AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[integer(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_2 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST('1' AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('1') to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_3 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST('+10 seconds' AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('+10 seconds') to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_4 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(CAST(1 AS NUMBER) AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[number(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_5 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(1e0 AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[double(1.0) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_6 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(true AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[boolean(TRUE) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_7 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(x'33' AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[varbinary(x'33') to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_8 = function()
    g.server:exec(function()
        local dt = require('datetime')
        local dt1 = dt.new({year = 2001, month = 1, day = 1, hour = 1})
        local sql = [[SELECT CAST(? AS INTERVAL);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[datetime(2001-01-01T01:00:00Z) to interval]]
        local _, err = box.execute(sql, {dt1})
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_18_9 = function()
    g.server:exec(function()
        local sql = [[SELECT CAST(
                      CAST('11111111-1111-1111-1111-111111111111' AS UUID)
                      AS INTERVAL) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[uuid(11111111-1111-1111-1111-111111111111) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that implicit cast from INTERVAL works as intended.
g.test_interval_19_1 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d UNSIGNED PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_2 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d INTEGER PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[integer]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_3 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d STRING PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[string]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_4 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d NUMBER PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[number]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_5 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d DOUBLE PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[double]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_6 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d BOOLEAN PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[boolean]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_7 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d VARBINARY PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[varbinary]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_8 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d SCALAR PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[scalar]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_9 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d UUID PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[uuid]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_10 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(d DATETIME PRIMARY KEY);]])
        local sql = [[INSERT INTO t SELECT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[datetime]]
        local _, err = box.execute(sql)
        box.execute([[DROP TABLE t;]])
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_19_11 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a ANY);]])
        box.execute([[INSERT INTO t SELECT i, itv FROM t0;]])
        local rows = box.execute([[SELECT a FROM t;]]).rows
        local res = {{itv1}, {itv2}}
        box.execute([[DROP TABLE t;]])
        t.assert_equals(rows, res)
    end)
end

g.test_interval_19_12 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, it INTERVAL);]])
        box.execute([[INSERT INTO t SELECT i, itv FROM t0;]])
        local rows = box.execute([[SELECT it FROM t;]]).rows
        local res = {{itv1}, {itv2}}
        box.execute([[DROP TABLE t;]])
        t.assert_equals(rows, res)
    end)
end

-- Make sure that implicit cast to INTERVAL works as intended.
g.test_interval_20_1 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, 1);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[integer(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_2 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, '1');]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('1') to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_3 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, '2001-01-01T01:00:00Z');]]
        local res = [[Type mismatch: can not convert ]]..
                    [[string('2001-01-01T01:00:00Z') to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_4 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, CAST(1 AS NUMBER));]]
        local res = [[Type mismatch: can not convert ]]..
                    [[number(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_5 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, 1e0);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[double(1.0) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_6 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, true);]]
        local res = [[Type mismatch: can not convert ]]..
                    [[boolean(TRUE) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_7 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3, x'33');]]
        local res = [[Type mismatch: can not convert ]]..
                    [[varbinary(x'33') to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_8 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 values(3, CAST(1 AS SCALAR));]]
        local res = [[Type mismatch: can not convert scalar(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_20_9 = function()
    g.server:exec(function()
        local sql = [[INSERT INTO t0 VALUES(3,
                      CAST('11111111-1111-1111-1111-111111111111' AS UUID));]]
        local res = [[Type mismatch: can not convert ]]..
                    [[uuid(11111111-1111-1111-1111-111111111111) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that INTERVAL can be used in trigger.
g.test_interval_21 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE ta (i INT PRIMARY KEY, itv INTERVAL);]])
        box.execute([[CREATE TABLE tb (i INT PRIMARY KEY, itv INTERVAL);]])
        box.execute([[CREATE TRIGGER tr AFTER INSERT ON ta
                      FOR EACH ROW BEGIN INSERT INTO tb
                      SELECT new.i, new.itv; END;]])

        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local itv2 = itv.new({min = 5, sec = 6, nsec = 7})
        box.execute([[INSERT INTO ta SELECT * FROM t0;]])
        local sql = [[SELECT itv FROM tb;]]
        local res = {{itv1}, {itv2}}
        local rows = box.execute(sql).rows

        box.execute([[DROP TABLE ta;]])
        box.execute([[DROP TABLE tb;]])

        t.assert_equals(rows, res)
    end)
end

-- Make sure that INTERVAL cannot work with JOINs.
g.test_interval_22_1 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, itv INTERVAL);]])
        box.execute([[INSERT INTO t SELECT * FROM t0;]])
        local sql = [[SELECT * FROM t0 JOIN t on t0.itv = t.itv;]]
        local res = [[Type mismatch: can not convert interval(+1 years, ]]..
                    [[2 months, 3 days, 4 hours) to comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_22_2 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, itv INTERVAL);]])
        box.execute([[INSERT INTO t SELECT * FROM t0;]])
        local sql = [[SELECT * FROM t0 LEFT JOIN t on t0.itv = t.itv;]]
        local res = [[Type mismatch: can not convert interval(+1 years, ]]..
                    [[2 months, 3 days, 4 hours) to comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_22_3 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, itv INTERVAL);]])
        box.execute([[INSERT INTO t SELECT * FROM t0;]])
        local sql = [[SELECT * FROM t0 INNER JOIN t on t0.itv = t.itv;]]
        local res = [[Type mismatch: can not convert interval(+1 years, ]]..
                    [[2 months, 3 days, 4 hours) to comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that numeric arithmetic operations work with INTERVAL as intended.
g.test_interval_23_1 = function()
    g.server:exec(function()
        local sql = [[SELECT -itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[integer, decimal or double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_23_2 = function()
    g.server:exec(function()
        local sql = [[SELECT itv + 1 FROM t0;]]
        local res = [[Type mismatch: can not convert integer(1) to datetime ]]..
                    [[or interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_23_3 = function()
    g.server:exec(function()
        local sql = [[SELECT itv - 1 FROM t0;]]
        local res = [[Type mismatch: can not convert integer(1) to interval]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_23_4 = function()
    g.server:exec(function()
        local sql = [[SELECT itv / 2 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[integer, decimal or double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_23_5 = function()
    g.server:exec(function()
        local sql = [[SELECT itv * 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[integer, decimal or double]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_23_6 = function()
    g.server:exec(function()
        local sql = [[SELECT itv % 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[integer]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that bitwise operations work with INTERVAL as intended.
g.test_interval_24_1 = function()
    g.server:exec(function()
        local sql = [[SELECT ~itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_24_2 = function()
    g.server:exec(function()
        local sql = [[SELECT itv >> 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_24_3 = function()
    g.server:exec(function()
        local sql = [[SELECT itv << 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_24_4 = function()
    g.server:exec(function()
        local sql = [[SELECT itv | 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_24_5 = function()
    g.server:exec(function()
        local sql = [[SELECT itv & 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[unsigned]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that logical operations work with INTERVAL as intended.
g.test_interval_25_1 = function()
    g.server:exec(function()
        local sql = [[SELECT NOT itv FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_25_2 = function()
    g.server:exec(function()
        local sql = [[SELECT itv AND true FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_25_3 = function()
    g.server:exec(function()
        local sql = [[SELECT itv OR true FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[boolean]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that comparison work with INTERVAL as intended.
g.test_interval_26_1 = function()
    g.server:exec(function()
        local sql = [[SELECT itv > 1 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_2 = function()
    g.server:exec(function()
        local sql = [[SELECT itv < '1' FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_3 = function()
    g.server:exec(function()
        local sql = [[SELECT itv == '2001-01-01T01:00:00Z' FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_4 = function()
    g.server:exec(function()
        local sql = [[SELECT itv >= CAST(1 AS NUMBER) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_5 = function()
    g.server:exec(function()
        local sql = [[SELECT itv <= 1e0 FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_6 = function()
    g.server:exec(function()
        local sql = [[SELECT itv > true FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_7 = function()
    g.server:exec(function()
        local sql = [[SELECT itv < x'33' FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_8 = function()
    g.server:exec(function()
        local sql = [[SELECT itv == CAST(1 AS SCALAR) FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

g.test_interval_26_9 = function()
    g.server:exec(function()
        local sql = [[SELECT itv >
                      CAST('11111111-1111-1111-1111-111111111111' AS UUID)
                      FROM t0;]]
        local res = [[Type mismatch: can not convert ]]..
                    [[interval(+1 years, 2 months, 3 days, 4 hours) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that DATETIME value can be bound.
g.test_datetime_27_1 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local rows = box.execute([[SELECT ?;]], {itv1}).rows
        t.assert_equals(rows, {{itv1}})
    end)
end

g.test_datetime_27_2 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local rows = box.execute([[SELECT $1;]], {itv1}).rows
        t.assert_equals(rows, {{itv1}})
    end)
end

g.test_datetime_27_3 = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
        local rows = box.execute([[SELECT #a;]], {{['#a'] = itv1}}).rows
        t.assert_equals(rows, {{itv1}})
    end)
end

g.test_datetime_27_4 = function()
    local conn = g.server.net_box
    local itv = require('datetime').interval
    local itv1 = itv.new({year = 1, month = 2, day = 3, hour = 4})
    local rows = conn:execute([[SELECT ?;]], {itv1}).rows
    t.assert_equals(rows, {{itv1}})
end
