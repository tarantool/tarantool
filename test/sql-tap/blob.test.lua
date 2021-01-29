#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(20)

--!./tcltestrunner.lua
-- 2001 September 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- $Id: blob.test,v 1.8 2009/04/28 18:00:27 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


local function bin_to_hex(blob)
    local bytes2 = {  }
    for i = 1, string.len(blob), 1 do
        string.byte("ABCDE")
        table.insert(bytes2, string.format("%02X", string.byte(blob, i)))
    end
    return table.concat(bytes2,  "")
end

-- Simplest possible case. Specify a blob literal
test:do_test(
    "blob-1.0",
    function()
        local blob = test:execsql "SELECT X'01020304';"
        return bin_to_hex(test.lindex(blob, 0))
    end, "01020304")

test:do_test(
    "blob-1.1",
    function()
        local blob = test:execsql "SELECT x'ABCDEF';"
        return bin_to_hex(test.lindex(blob, 0))
    end, "ABCDEF")

test:do_test(
    "blob-1.2",
    function()
        local blob = test:execsql "SELECT x'';"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "blob-1.3",
    function()
        local blob = test:execsql "SELECT x'abcdEF12';"
        return bin_to_hex(test.lindex(blob, 0))
    end, "ABCDEF12")

test:do_test(
    "blob-1.3.2",
    function()
        local blob = test:execsql "SELECT x'0123456789abcdefABCDEF';"
        return bin_to_hex(test.lindex(blob, 0))
    end, "0123456789ABCDEFABCDEF")

-- Try some syntax errors in blob literals.
test:do_catchsql_test(
    "blob-1.4",
    [[
        SELECT X'01020k304', 100
    ]], {
        -- <blob-1.4>
        1, [[At line 1 at or near position 16: unrecognized token 'X'01020k304'']]
        -- </blob-1.4>
    })

test:do_catchsql_test(
    "blob-1.5",
    [[
        SELECT X'01020, 100]], {
        -- <blob-1.5>
        1, [[At line 1 at or near position 16: unrecognized token 'X'01020, 100']]
        -- </blob-1.5>
    })

test:do_catchsql_test(
    "blob-1.6",
    [[
        SELECT X'01020 100'
    ]], {
        -- <blob-1.6>
        1, [[At line 1 at or near position 16: unrecognized token 'X'01020 100'']]
        -- </blob-1.6>
    })

test:do_catchsql_test(
    "blob-1.7",
    [[
        SELECT X'01001'
    ]], {
        -- <blob-1.7>
        1, [[At line 1 at or near position 16: unrecognized token 'X'01001'']]
        -- </blob-1.7>
    })

test:do_catchsql_test(
    "blob-1.8",
    [[
        SELECT x'012/45'
    ]], {
        -- <blob-1.8>
        1, [[At line 1 at or near position 16: unrecognized token 'x'012/45'']]
        -- </blob-1.8>
    })

test:do_catchsql_test(
    "blob-1.9",
    [[
        SELECT x'012:45'
    ]], {
        -- <blob-1.9>
        1, [[At line 1 at or near position 16: unrecognized token 'x'012:45'']]
        -- </blob-1.9>
    })

test:do_catchsql_test(
    "blob-1.10",
    [[
        SELECT x'012@45'
    ]], {
        -- <blob-1.10>
        1, [[At line 1 at or near position 16: unrecognized token 'x'012@45'']]
        -- </blob-1.10>
    })

test:do_catchsql_test(
    "blob-1.11",
    [[
        SELECT x'012G45'
    ]], {
        -- <blob-1.11>
        1, [[At line 1 at or near position 16: unrecognized token 'x'012G45'']]
        -- </blob-1.11>
    })

test:do_catchsql_test(
    "blob-1.12",
    [[
        SELECT x'012`45'
    ]], {
        -- <blob-1.12>
        1, [[At line 1 at or near position 16: unrecognized token 'x'012`45'']]
        -- </blob-1.12>
    })

test:do_catchsql_test(
    "blob-1.13",
    [[
        SELECT x'012g45'
    ]], {
        -- <blob-1.13>
        1, [[At line 1 at or near position 16: unrecognized token 'x'012g45'']]
        -- </blob-1.13>
    })

-- Insert a blob into a table and retrieve it.
test:do_test(
    "blob-2.0",
    function()
        test:execsql [[
            CREATE TABLE t1(a SCALAR primary key, b SCALAR);
            INSERT INTO t1 VALUES(X'123456', x'7890ab');
            INSERT INTO t1 VALUES(X'CDEF12', x'345678');
        ]]
        local blobs, blobs2
        blobs = test:execsql "SELECT * FROM t1"
        blobs2 = {  }
        for _, b in ipairs(blobs) do
            table.insert(blobs2,bin_to_hex(b))
        end
        return blobs2
    end, {
        -- <blob-2.0>
        "123456", "7890AB", "CDEF12", "345678"
        -- </blob-2.0>
    })

-- An index on a SCALAR column
test:do_test(
    "blob-2.1",
    function()
        test:execsql [[
            CREATE INDEX i1 ON t1(a);
        ]]
        local blobs, blobs2
        blobs = test:execsql "SELECT * FROM t1"
        blobs2 = {  }
        for _, b in ipairs(blobs) do
            table.insert(blobs2,bin_to_hex(b))
        end
        return blobs2
    end, {
        -- <blob-2.1>
        "123456", "7890AB", "CDEF12", "345678"
        -- </blob-2.1>
    })

test:do_test(
    "blob-2.2",
    function()
        local blobs, blobs2
        blobs = test:execsql "SELECT * FROM t1 where a = X'123456'"
        blobs2 = {  }
        for _, b in ipairs(blobs) do
            table.insert(blobs2,bin_to_hex(b))
        end
        return blobs2
    end, {
        -- <blob-2.2>
        "123456", "7890AB"
        -- </blob-2.2>
    })

test:do_test(
    "blob-2.3",
    function()
        local blobs, blobs2
        blobs = test:execsql "SELECT * FROM t1 where a = X'CDEF12'"
        blobs2 = {  }
        for _, b in ipairs(blobs) do
            table.insert(blobs2,bin_to_hex(b))
        end
        return blobs2
    end, {
        -- <blob-2.3>
        "CDEF12", "345678"
        -- </blob-2.3>
    })

test:do_test(
    "blob-2.4",
    function()
        local blobs, blobs2
        blobs = test:execsql "SELECT * FROM t1 where a = X'CD12'"
        blobs2 = {  }
        for _, b in ipairs(blobs) do
            table.insert(blobs2,bin_to_hex(b))
        end
        return blobs2
    end, {
        -- <blob-2.4>

        -- </blob-2.4>
    })

-- Try to bind a blob value to a prepared statement.
-- Tarantool won't support attaching to multiple DBs
-- do_test blob-3.0 {
--   sql db2 test.db
--   set DB [sql_connection_pointer db2]
--   set STMT [sql_prepare $DB "DELETE FROM t1 WHERE a = ?" -1 DUMMY]
--   sql_bind_blob $STMT 1 "\x12\x34\x56" 3
--   sql_step $STMT
-- } {sql_DONE}
-- do_test blob-3.1 {
--   sql_finalize $STMT
--   db2 close
-- } {}
-- MUST_WORK_TEST
if (0 > 0)
 then
    test:do_test(
        "blob-3.2",
        function()
            local blobs, blobs2
            blobs = test:execsql "SELECT * FROM t1"
            blobs2 = {  }
            for _, b in ipairs(blobs) do
                table.insert(blobs2,bin_to_hex(b))
            end
            return blobs2
        end, {
            -- <blob-3.2>
            "CDEF12", "345678"
            -- </blob-3.2>
        })

end


test:finish_test()
