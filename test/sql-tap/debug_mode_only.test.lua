#!/usr/bin/env tarantool
-- This file contains testcases which can only be valudated in debug mode.
test = require("sqltester")
test:plan(30)

local ffi = require("ffi")

ffi.cdef("extern int sql_current_time;")

local function datetest(tnum, expr, result)
    test:do_test(
        "date-"..tnum,
        function ()
            local r = test:execsql("SELECT coalesce( "..expr..", 'NULL' )")
            return ""..r[1]
        end,
        result)
end

ffi.C.sql_current_time = 1157124367
datetest(4.1, "date('now')", "2006-09-01")
ffi.C.sql_current_time = 0
ffi.C.sql_current_time = 1199243045
datetest(2.40, "datetime()", "2008-01-02 03:04:05")
ffi.C.sql_current_time = 0
-- Test modifiers when the date begins as a julian day number - to
-- make sure the HH:MM:SS is preserved.  Ticket #551.
--
-- TBI to be implemented ffi.C.sql_current_time
ffi.C.sql_current_time =
    tonumber(test:execsql("SELECT strftime('%s','2003-10-22 12:34:00')")[1])
datetest(8.1, "datetime('now','weekday 0')", "2003-10-26 12:34:00")
datetest(8.2, "datetime('now','weekday 1')", "2003-10-27 12:34:00")
datetest(8.3, "datetime('now','weekday 2')", "2003-10-28 12:34:00")
datetest(8.4, "datetime('now','weekday 3')", "2003-10-22 12:34:00")
datetest(8.5, "datetime('now','start of month')", "2003-10-01 00:00:00")
datetest(8.6, "datetime('now','start of year')", "2003-01-01 00:00:00")
datetest(8.7, "datetime('now','start of day')", "2003-10-22 00:00:00")
datetest(8.8, "datetime('now','1 day')", "2003-10-23 12:34:00")
datetest(8.9, "datetime('now','+1 day')", "2003-10-23 12:34:00")
datetest(8.10, "datetime('now','+1.25 day')", "2003-10-23 18:34:00")
datetest(8.11, "datetime('now','-1.0 day')", "2003-10-21 12:34:00")
datetest(8.12, "datetime('now','1 month')", "2003-11-22 12:34:00")
datetest(8.13, "datetime('now','11 month')", "2004-09-22 12:34:00")
datetest(8.14, "datetime('now','-13 month')", "2002-09-22 12:34:00")
datetest(8.15, "datetime('now','1.5 months')", "2003-12-07 12:34:00")
datetest(8.16, "datetime('now','-5 years')", "1998-10-22 12:34:00")
datetest(8.17, "datetime('now','+10.5 minutes')", "2003-10-22 12:44:30")
datetest(8.18, "datetime('now','-1.25 hours')", "2003-10-22 11:19:00")
datetest(8.19, "datetime('now','11.25 seconds')", "2003-10-22 12:34:11")
datetest(8.90, "datetime('now','abcdefghijklmnopqrstuvwyxzABCDEFGHIJLMNOP')", "NULL")
ffi.C.sql_current_time = 0
--
-- Test the ability to have default values of CURRENT_TIME, CURRENT_DATE
-- and CURRENT_TIMESTAMP.
--
test:do_execsql_test(
    "table-13.1",
    [[
        CREATE TABLE tablet8(
           a integer primary key,
           tm text DEFAULT CURRENT_TIME,
           dt text DEFAULT CURRENT_DATE,
           dttm text DEFAULT CURRENT_TIMESTAMP
        );
        SELECT * FROM tablet8;
    ]], {})

local data = {
    {"1976-07-04", "12:00:00", 205329600},
    {"1994-04-16", "14:00:00", 766504800},
    {"2000-01-01", "00:00:00", 946684800},
    {"2003-12-31", "12:34:56", 1072874096},
}
for i ,val in ipairs(data) do
    local date = val[1]
    local time = val[2]
    local seconds = val[3]
    ffi.C.sql_current_time = seconds
    test:do_execsql_test(
        "table-13.2."..i,
        string.format([[
            INSERT INTO tablet8(a) VALUES(%s);
            SELECT tm, dt, dttm FROM tablet8 WHERE a=%s;
        ]], i, i), {
            time, date, date .. " " .. time
        })

end
ffi.C.sql_current_time = 1
test:do_execsql_test(
    "e_expr-12.2.6",
    [[
        SELECT CURRENT_TIME
    ]], {
        -- <e_expr-12.2.6>
        "00:00:01"
        -- </e_expr-12.2.6>
    })

test:do_execsql_test(
    "e_expr-12.2.7",
    [[
        SELECT CURRENT_DATE
    ]], {
        -- <e_expr-12.2.7>
        "1970-01-01"
        -- </e_expr-12.2.7>
    })

test:do_execsql_test(
    "e_expr-12.2.8",
    [[
        SELECT CURRENT_TIMESTAMP
    ]], {
        -- <e_expr-12.2.8>
        "1970-01-01 00:00:01"
        -- </e_expr-12.2.8>
    })

ffi.C.sql_current_time = 0

test:finish_test()
