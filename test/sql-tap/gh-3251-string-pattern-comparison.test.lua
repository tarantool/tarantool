#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(128)

local prefix = "like-test-"

local like_test_cases =
{
    {"1.1",
        "SELECT 'AB' LIKE '_B';",
        {0, {true}} },
    {"1.2",
        "SELECT 'CD' LIKE '_B';",
        {0, {false}} },
    {"1.3",
        "SELECT '' LIKE '_B';",
        {0, {false}} },
    {"1.4",
        "SELECT 'AB' LIKE '%B';",
        {0, {true}} },
    {"1.5",
        "SELECT 'CD' LIKE '%B';",
        {0, {false}} },
    {"1.6",
        "SELECT '' LIKE '%B';",
        {0, {false}} },
    {"1.7",
        "SELECT 'AB' LIKE 'A__';",
        {0, {false}} },
    {"1.8",
        "SELECT 'CD' LIKE 'A__';",
        {0, {false}} },
    {"1.9",
        "SELECT '' LIKE 'A__';",
        {0, {false}} },
    {"1.10",
        "SELECT 'AB' LIKE 'A_';",
        {0, {true}} },
    {"1.11",
        "SELECT 'CD' LIKE 'A_';",
        {0, {false}} },
    {"1.12",
        "SELECT '' LIKE 'A_';",
        {0, {false}} },
    {"1.13",
        "SELECT 'AB' LIKE 'A';",
        {0, {false}} },
    {"1.14",
        "SELECT 'CD' LIKE 'A';",
        {0, {false}} },
    {"1.15",
        "SELECT '' LIKE 'A';",
        {0, {false}} },
    {"1.16",
        "SELECT 'AB' LIKE '_';",
        {0, {false}} },
    {"1.17",
        "SELECT 'CD' LIKE '_';",
        {0, {false}} },
    {"1.18",
        "SELECT '' LIKE '_';",
        {0, {false}} },
    {"1.19",
        "SELECT 'AB' LIKE '__';",
        {0, {true}} },
    {"1.20",
        "SELECT 'CD' LIKE '__';",
        {0, {true}} },
    {"1.21",
        "SELECT '' LIKE '__';",
        {0, {false}} },
    {"1.22",
        "SELECT 'AB' LIKE '%A';",
        {0, {false}} },
    {"1.23",
        "SELECT 'AB' LIKE '%C';",
        {0, {false}} },
    {"1.24",
        "SELECT 'ёф' LIKE '%œش';",
        {0, {false}} },
    {"1.25",
        "SELECT 'ёфÅŒش' LIKE '%œش';",
        {0, {true}} },
    {"1.26",
        "SELECT 'ÅŒش' LIKE '%œش';",
        {0, {true}} },
    {"1.27",
        "SELECT 'ёф' LIKE 'ё_';",
        {0, {true}} },
    {"1.28",
        "SELECT 'ёфÅŒش' LIKE 'ё_';",
        {0, {false}} },
    {"1.29",
        "SELECT 'ÅŒش' LIKE 'ё_';",
        {0, {false}} },
    {"1.30",
        "SELECT 'ёф' LIKE 'ёф%';",
        {0, {true}} },
    {"1.31",
        "SELECT 'ёфÅŒش' LIKE 'ёф%';",
        {0, {true}} },
    {"1.32",
        "SELECT 'ÅŒش' LIKE 'ёф%';",
        {0, {false}} },
    {"1.33",
        "SELECT 'ёф' LIKE 'ёфÅ%';",
        {0, {false}} },
    {"1.34",
        "SELECT 'ёфÅŒش' LIKE 'ёфÅ%';",
        {0, {true}} },
    {"1.35",
        "SELECT 'ÅŒش' LIKE 'ёфش%';",
        {0, {false}} },
    {"1.36",
        "SELECT 'ёф' LIKE 'ё_%';",
        {0, {true}} },
    {"1.37",
        "SELECT 'ёфÅŒش' LIKE 'ё_%';",
        {0, {true}} },
    {"1.38",
        "SELECT 'ÅŒش' LIKE 'ё_%';",
        {0, {false}} },
}

test:do_catchsql_set_test(like_test_cases, prefix)

-- Non-Unicode byte sequences.
local invalid_testcases = {
    '\xE2\x80',
    '\xFE\xFF',
    '\xC2',
    '\xED\xB0\x80',
    '\xD0',
}

-- Invalid unicode symbols.
for i, tested_string in ipairs(invalid_testcases) do

    -- We should raise an error in case
    -- pattern contains invalid characters.

    local test_name = prefix .. "2." .. tostring(i)
    local test_itself = "SELECT 'abc' LIKE 'ab" .. tested_string .. "';"
    test:do_catchsql_test(test_name, test_itself,
                          {1, "Failed to execute SQL statement: LIKE pattern can only contain UTF-8 characters"})

    test_name = prefix .. "3." .. tostring(i)
    test_itself = "SELECT 'abc' LIKE 'abc" .. tested_string .. "';"
    test:do_catchsql_test(test_name, test_itself,
                          {1, "Failed to execute SQL statement: LIKE pattern can only contain UTF-8 characters"})

    test_name = prefix .. "4." .. tostring(i)
    test_itself = "SELECT 'abc' LIKE 'ab" .. tested_string .. "c';"
    test:do_catchsql_test(test_name, test_itself,
                          {1, "Failed to execute SQL statement: LIKE pattern can only contain UTF-8 characters"})

    -- Just skipping if row value predicand contains invalid character.

    test_name = prefix .. "5." .. tostring(i)
    test_itself = "SELECT 'ab" .. tested_string .. "' LIKE 'abc';"
    test:do_execsql_test(test_name, test_itself, {false})

    test_name = prefix .. "6." .. tostring(i)
    test_itself = "SELECT 'abc" .. tested_string .. "' LIKE 'abc';"
    test:do_execsql_test(test_name, test_itself, {false})

    test_name = prefix .. "7." .. tostring(i)
    test_itself = "SELECT 'ab" .. tested_string .. "c' LIKE 'abc';"
    test:do_execsql_test(test_name, test_itself, {false})
end

-- Unicode byte sequences.
local valid_testcases = {
    '\x01',
    '\x09',
    '\x1F',
    '\x7F',
    '\xC2\x80',
    '\xC2\x90',
    '\xC2\x9F',
    '\xE2\x80\xA8',
    '\x20\x0B',
    '\xE2\x80\xA9',
}

-- Valid unicode symbols.
for i, tested_string in ipairs(valid_testcases) do
    local test_name = prefix .. "8." .. tostring(i)
    local test_itself = "SELECT 'abc' LIKE 'ab" .. tested_string .. "';"
    test:do_execsql_test(test_name, test_itself, {false})

    test_name = prefix .. "9." .. tostring(i)
    test_itself = "SELECT 'abc' LIKE 'abc" .. tested_string .. "';"
    test:do_execsql_test(test_name, test_itself, {false})

    test_name = prefix .. "10." .. tostring(i)
    test_itself = "SELECT 'abc' LIKE 'ab" .. tested_string .. "c';"
    test:do_execsql_test(test_name,	test_itself, {false})

    test_name = prefix .. "11." .. tostring(i)
    test_itself = "SELECT 'ab" .. tested_string .. "' LIKE 'abc';"
    test:do_execsql_test(test_name,	test_itself, {false})

    test_name = prefix .. "12." .. tostring(i)
    test_itself = "SELECT 'abc" .. tested_string .. "' LIKE 'abc';"
    test:do_execsql_test(test_name, test_itself, {false})

    test_name = prefix .. "13." .. tostring(i)
    test_itself = "SELECT 'ab" .. tested_string .. "c' LIKE 'abc';"
    test:do_execsql_test(test_name, test_itself, {false})
end

test:finish_test()
