local tap = require('tap')
local json = require('json')
local test = tap.test("errno")

local function flatten(arr)
    local result = { }

    local function flatten(arr)
        for _, v in ipairs(arr) do
            if type(v) == "table" then
                flatten(v)
            else
                table.insert(result, v)
            end
        end
    end
    flatten(arr)
    return result
end

-- Goal of this routine is to update expected result
-- to be comparable with expected.
-- Right now it converts logical values to numbers.
-- Input must be a table.
local function fix_result(arr)
    if type(arr) ~= 'table' then return arr end
    for i, v in ipairs(arr) do
        if type(v) == 'table' then
            -- it is ok to pass array
            --fix_expect(v)
        else
            if type(v) == 'boolean' then
                if v then
                    arr[i] = 1
                else
                    arr[i] = 0
                end
            end
        end
    end
end

local function finish_test()
    test:check()
    os.exit()
end
test.finish_test = finish_test

-- Check if string is regex pattern.
-- Condition: /.../ or ~/.../
local function string_regex_p(str)
    if type(str) == 'string'
            and (string.sub(str, 1, 1) == '/'
            or string.sub(str, 1, 2) == '~/')
            and string.sub(str, -1) == '/' then
        return true;
    else
        return false;
    end
end

local function table_check_regex_p(t, regex)
    -- regex is definetely regex here, no additional checks
    local nmatch = string.sub(regex, 1, 1) == '~' and 1 or 0
    local regex_tr = string.sub(regex, 2 + nmatch, string.len(regex) - 1)
    for _, v in pairs(t) do
        if nmatch == 1 then
            if type(v) == 'table' and not table_check_regex_p(v, regex) then
                return 0
            end
            if type(v) == 'string' and string.find(v, regex_tr) then
                return 0
            end
        else
            if type(v) == 'table' and table_check_regex_p(v, regex) then
                return 1
            end
            if type(v) == 'string' and string.find(v, regex_tr) then
                return 1
            end
        end
    end

    return nmatch
end

local function is_deeply_regex(got, expected)
    if type(expected) == "number" or type(got) == "number" then
        if got ~= got and expected ~= expected then
            return true -- nan
        end
    end
    if type(expected) == "number" and type(got) == "number" then
        local min_delta = 0.0000001
        return (got - expected < min_delta) and (expected - got < min_delta)
    end

    if string_regex_p(expected) then
        return table_match_regex_p(got, expected)
    end

    if got == nil and expected == nil then return true end

    if type(got) ~= type(expected) then
        return false
    end

    if type(got) ~= 'table' then
        return got == expected
    end

    for i, v in pairs(expected) do
        if string_regex_p(v) then
            return table_check_regex_p(got, v) == 1
        else
            if not is_deeply_regex(got[i], v) then
                return false
            end
        end
    end

    if #got ~= #expected then
        return false
    end

    return true
end
test.is_deeply_regex = is_deeply_regex

local function do_test(self, label, func, expect)
    local ok, result = pcall(func)
    if ok then
        if result == nil then result = { } end
        -- Convert all trues and falses to 1s and 0s
        fix_result(result)

        -- If nothing is expected: just make sure there were no error.
        if expect == nil then
            if table.getn(result) ~= 0 and result[1] ~= 0 then
                test:fail(self, label)
            else
                test:ok(self, label)
            end
        else
            if is_deeply_regex(result, expect) then
                test:ok(self, label)
            else
                io.write(string.format('%s: Miscompare\n', label))
                io.write("Expected: ", json.encode(expect).."\n")
                io.write("Got     : ", json.encode(result).."\n")
                test:fail(label)
            end
        end
    else
        self:fail(string.format('%s: Execution failed: %s\n', label, result))
    end
end
test.do_test = do_test

local function execsql(self, sql)
    local result = box.sql.execute(sql)
    if type(result) ~= 'table' then return end

    result = flatten(result)
    for i, c in ipairs(result) do
        if c == nil then
            result[i] = ""
        end
    end
    return result
end
test.execsql = execsql

local function catchsql(self, sql, expect)
    r = {pcall(execsql, self, sql) }
    if r[1] == true then
        r[1] = 0
    else
        r[1] = 1
    end
    return r
end
test.catchsql = catchsql

local function do_catchsql_test(self, label, sql, expect)
    return do_test(self, label, function() return catchsql(self, sql) end, expect)
end
test.do_catchsql_test = do_catchsql_test

local function do_catchsql2_test(self, label, sql, expect)
    return do_test(self, label, function() return test.catchsql2(self, sql) end, expect)
end
test.do_catchsql2_test = do_catchsql2_test

local function do_execsql_test(self, label, sql, expect)
    return do_test(self, label, function() return test.execsql(self, sql) end, expect)
end
test.do_execsql_test = do_execsql_test

local function do_execsql2_test(self, label, sql, expect)
    return do_test(self, label, function() return test.execsql2(self, sql) end, expect)
end
test.do_execsql2_test = do_execsql2_test

local function flattern_with_column_names(result)
    local ret = {}
    local columns = result[0]
    for i = 1, #result, 1 do
        for j = 1, #columns, 1 do
            table.insert(ret, columns[j])
            table.insert(ret, result[i][j])
        end
    end
    return ret
end

local function execsql2(self, sql)
    local result = box.sql.execute(sql)
    if type(result) ~= 'table' then return end
    -- shift rows down, revealing column names
    result = flattern_with_column_names(result)
    return result
end
test.execsql2 = execsql2

local function sortsql(self, sql)
    local result = execsql(self, sql)
    table.sort(result, function(a,b) return a[2] < b[2] end)
    return result
end
test.sortsql = sortsql

local function catchsql2(self, sql)
    r = {pcall(execsql2, self, sql) }
    -- 0 means ok
    -- 1 means not ok
    r[1] = r[1] == true and 0 or 1
    return r
end
test.catchsql2 = catchsql2

-- Show the VDBE program for an SQL statement but omit the Trace
-- opcode at the beginning.  This procedure can be used to prove
-- that different SQL statements generate exactly the same VDBE code.
local function explain_no_trace(self, sql)
    tr = execsql(self, "EXPLAIN "..sql)
    for i=1,8 do
        table.remove(tr,1)
    end
    return tr
end
test.explain_no_trace = explain_no_trace

test.do_eqp_test = function (self, label, sql, result)

    test:do_test(
        label,
        function()
            return box.sql.execute("EXPLAIN QUERY PLAN "..sql)
        end,
        result
    )
end

function test.drop_all_tables(self)
    local tables = test:execsql("SELECT name FROM _space WHERE name NOT LIKE '\\_%' ESCAPE '\\';")
    for _, table in ipairs(tables) do
        test:execsql("DROP TABLE "..table..";")
    end
end

function test.do_select_tests(self, label, tests)
    for _, test_case in ipairs(tests) do
        local tn = test_case[1]
        local sql = test_case[2]
        local result = test_case[3]
        test:do_test(
            label..'.'..tn,
            function()
                return test:execsql(sql)
            end,
            result)
    end
end

local function db(self, cmd, ...)
    if cmd == 'eval' then
        return execsql(self, ...)
    end
end
test.db = db

local function lsearch(self, input, seed)
    local result = 0

    local function search(arr)
        if type(arr) == 'table' then
            for _, v in ipairs(arr) do
                search(v)
            end
        else
            if type(arr) == 'string' and arr:find(seed) ~= nil then
                result = result + 1
            end
        end
    end

    search(input)
    return result
end
test.lsearch = lsearch

function test.lindex(arr, pos)
    return arr[pos+1]
end

--function capable()
--    return true
--end

setmetatable(_G, nil)
os.execute("rm -f *.snap *.xlog*")

-- start the database
box.cfg{
    memtx_max_tuple_size=4996109,
}
return test
