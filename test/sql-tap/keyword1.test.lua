#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(184)

--!./tcltestrunner.lua
-- 2009 January 29
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- Verify that certain keywords can be used as identifiers.
--
-- $Id: keyword1.test,v 1.1 2009/01/29 19:27:47 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:execsql [[
    CREATE TABLE t1(a INT PRIMARY KEY, b TEXT);
    INSERT INTO t1 VALUES(1, 'one');
    INSERT INTO t1 VALUES(2, 'two');
    INSERT INTO t1 VALUES(3, 'three');
]]
local kwlist = {
	"abort",
	"action",
	"after",
	"cascade",
	"before",
	"conflict",
	"deferred",
	"engine",
	"fail",
	"ignore",
	"initially",
	"instead",
	"key",
	"offset",
	"plan",
	"query",
	"restrict",
	"raise",
	"enable",
	"disable"
}

local bannedkws = {
	"all",
	"alter",
	"analyze",
	"and",
	"as",
	"asc",
	"begin",
	"between",
	"blob",
	"by",
	"case",
	"check",
	"collate",
	"column",
	"commit",
	"constraint",
	"create",
	"cross",
	"current_date",
	"current_time",
	"current_timestamp",
	"default",
	"delete",
	"desc",
	"distinct",
	"drop",
	"each",
	"end",
	"else",
	"escape",
	"except",
	"exists",
	"explain",
	"for",
	"foreign",
	"from",
	"group",
	"having",
	"immediate",
	"in",
	"index",
	"inner",
	"insert",
	"intersect",
	"into",
	"is",
	"join",
	"left",
	"like",
	"match",
	"natural",
	"not",
	"null",
	"of",
	"on",
	"or",
	"order",
	"outer",
	"pragma",
	"primary",
	"recursive",
	"references",
	"release",
	"rename",
	"replace",
	"right",
	"rollback",
	"row",
	"savepoint",
	"select",
	"set",
	"table",
	"then",
	"to",
	"transaction",
	"trigger",
	"union",
	"unique",
	"update",
	"using",
	"values",
	"view",
	"with",
	"when",
	"where",
	"any",
	"asensitive",
	"binary",
	"call",
	"char",
	"character",
	"condition",
	"connect",
	"current",
	"current_user",
	"cursor",
	"date",
	"decimal",
	"declare",
	"dense_rank",
	"describe",
	"deterministic",
	"double",
	"elseif",
	"fetch",
	"float",
	"function",
	"get",
	"grant",
	"integer",
	"inout",
	"insensitive",
	"iterate",
	"leave",
	"localtime",
	"localtimestamp",
	"loop",
	"number",
	"out",
	"over",
	"partition",
	"precision",
	"procedure",
	"range",
	"rank",
	"reads",
	"repeat",
	"resignal",
	"return",
	"revoke",
	"rows",
	"row_number",
	"sensitive",
	"signal",
	"smallint",
	"specific",
	"start",
	"system",
	"sql",
	"user",
	"varchar",
	"varbinary",
	"whenever",
	"while"
}
local exprkw = [[
    "cast",
    "current_date",
    "current_time",
    "current_timestamp",
    "raise",
]]
for _, kw in ipairs(kwlist) do
    test:do_test(
        "keyword1-"..kw..".1",
        function()
            if (kw == "if") then
                test:execsql( string.format([[CREATE TABLE "%s"(%s %s PRIMARY KEY)]], kw:upper(), kw, 'INT'))
            else
                test:execsql(string.format("CREATE TABLE %s(%s %s PRIMARY KEY)", kw, kw, 'INT'))
            end
            test:execsql("INSERT INTO "..kw.." VALUES(99)")
            test:execsql("INSERT INTO "..kw.." SELECT a FROM t1")
            if test:lsearch(exprkw, kw) <0 then
                return test:execsql(string.format("SELECT * FROM %s ORDER BY %s ASC", kw, kw))
            else
                return test:execsql(string.format([[SELECT * FROM %s ORDER BY "%s" ASC]], kw, kw:upper()))
            end
        end, {
            1, 2, 3, 99
        })

    test:do_test(
        "keyword1-"..kw..".2",
        function()
            if (kw == "if") then
                test:execsql(string.format([[DROP TABLE "%s"]], kw:upper()))
                test:execsql(string.format([[CREATE INDEX "%s" ON t1(a)]], kw))
            else
                test:execsql("DROP TABLE "..kw.."")
                test:execsql("CREATE INDEX "..kw.." ON t1(a)")
            end
            return test:execsql("SELECT b FROM t1 WHERE a=2")
        end, {
            "two"
        })

end

for _, kw in ipairs(bannedkws) do
    local query = 'CREATE TABLE '..kw..'(a INT PRIMARY KEY);'
    if kw == 'end' or kw == 'match' or kw == 'release' or kw == 'rename' or
       kw == 'replace' or kw == 'binary' or kw == 'character' or
       kw == 'smallint' then
        test:do_catchsql_test(
        "bannedkw1-"..kw..".1",
        query, {
            1, "At line 1 at or near position "..14 + string.len(kw)..
            ": keyword '"..kw.."' is reserved. Please use double quotes if '"
            ..kw.."' is an identifier."
        })
    else
        test:do_catchsql_test(
        "bannedkw1-"..kw..".1",
        query, {
            1, "At line 1 at or near position 14: keyword '"..kw..
            "' is reserved. Please use double quotes if '"..kw..
            "' is an identifier."
        })
    end
end


test:finish_test()
