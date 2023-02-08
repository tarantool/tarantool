local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'index'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure that ASC/DESC are no longer supported during index creation.
g.test_asc_desc_in_idx_def = function()
    g.server:exec(function()
        local _, err, sql, res

        sql = [[CREATE TABLE t (a INT PRIMARY KEY ASC, b INT);]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 35: keyword 'ASC' is reserved. "..
              "Please use double quotes if 'ASC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT PRIMARY KEY DESC, b INT);]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 35: keyword 'DESC' is reserved. "..
              "Please use double quotes if 'DESC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT, b INT, PRIMARY KEY(a ASC, b));]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 45: keyword 'ASC' is reserved. "..
              "Please use double quotes if 'ASC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT, b INT, PRIMARY KEY(a, b DESC);]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 48: keyword 'DESC' is reserved. "..
              "Please use double quotes if 'DESC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT PRIMARY KEY, a INT UNIQUE ASC);]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 49: keyword 'ASC' is reserved. "..
              "Please use double quotes if 'ASC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT PRIMARY KEY, a INT UNIQUE DESC);]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 49: keyword 'DESC' is reserved. "..
              "Please use double quotes if 'DESC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT PRIMARY KEY, a INT, UNIQUE (a, b ASC));]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 56: keyword 'ASC' is reserved. "..
              "Please use double quotes if 'ASC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t (a INT PRIMARY KEY, a INT, UNIQUE (a DESC, b));]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 53: keyword 'DESC' is reserved. "..
              "Please use double quotes if 'DESC' is an identifier."
        t.assert_equals(err.message, res)

        box.execute([[CREATE TABLE t (a INT PRIMARY KEY, a INT);]])

        sql = [[CREATE INDEX i ON t(a ASC, b));]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 23: keyword 'ASC' is reserved. "..
              "Please use double quotes if 'ASC' is an identifier."
        t.assert_equals(err.message, res)

        sql = [[CREATE INDEX i ON t(a, b DESC));]]
        _, err = box.execute(sql)
        res = "At line 1 at or near position 26: keyword 'DESC' is reserved. "..
              "Please use double quotes if 'DESC' is an identifier."
        t.assert_equals(err.message, res)

        box.execute([[DROP TABLE t;]])
    end)
end
