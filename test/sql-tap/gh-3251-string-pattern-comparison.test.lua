#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(162)

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
        "SELECT 'ёфÅŒش' LIKE '%œش' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.26",
        "SELECT 'ÅŒش' LIKE '%œش' COLLATE \"unicode_ci\";",
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
    {"1.39",
        "SELECT 'A' LIKE 'A' COLLATE \"unicode\";",
        {0, {true}} },
    {"1.40",
        "SELECT 'A' LIKE 'a' COLLATE \"unicode\";",
        {0, {false}} },
    {"1.41",
        "SELECT 'Ab' COLLATE \"unicode\" LIKE 'ab';",
        {0, {false}} },
    {"1.42",
        "SELECT 'ss' LIKE 'ß' COLLATE \"unicode\";",
        {0, {false}} },
    {"1.43",
        "SELECT 'Я' LIKE 'я' COLLATE \"unicode\";",
        {0, {false}} },
    {"1.44",
        "SELECT 'AЯB' LIKE 'AяB' COLLATE \"unicode\";",
        {0, {false}} },
    {"1.45",
        "SELECT 'Ї' LIKE 'ї' COLLATE \"unicode\";",
        {0, {false}} },
    {"1.46",
        "SELECT 'Ab' LIKE '_b' COLLATE \"unicode\";",
        {0, {true}} },
    {"1.47",
        "SELECT 'A' LIKE '_' COLLATE \"unicode\";",
        {0, {true}} },
    {"1.48",
        "SELECT 'AB' LIKE '%B' COLLATE \"unicode\";",
        {0, {true}} },
    {"1.49",
        "SELECT 'A' LIKE 'A' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.50",
        "SELECT 'A' COLLATE \"unicode_ci\" LIKE 'a';",
        {0, {true}} },
    {"1.51",
        "SELECT 'Я' LIKE 'я' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.52",
        "SELECT 'AЯB' LIKE 'AяB' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.53",
        "SELECT 'Ї' LIKE 'ї' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.54",
        "SELECT 'Ab' LIKE 'ab' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.55",
        "SELECT 'ba' LIKE 'ab' COLLATE \"unicode_ci\";",
        {0, {false}} },
    {"1.56",
        "SELECT 'Aaa' LIKE 'A%' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.57",
        "SELECT 'aaa' LIKE 'A%' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.58",
        "SELECT 'A' LIKE '_' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.59",
        "SELECT 'ss' LIKE 'ß' COLLATE \"unicode_de__phonebook_s1\";",
        {0, {false}} },
    {"1.60",
        "SELECT 'ss' LIKE 'ß' COLLATE \"unicode_de__phonebook_s3\";",
        {0, {false}} },
    {"1.61",
        "SELECT 'Ї' LIKE 'ї' COLLATE \"unicode_uk_s1\";",
        {0, {true}} },
    {"1.62",
        "SELECT 'Ї' LIKE 'ї' COLLATE \"unicode_uk_s3\";",
        {0, {false}} },
    {"1.63",
        "SELECT 'Ї' COLLATE \"unicode_uk_s3\" LIKE 'ї' COLLATE \"unicode_uk_s3\";",
        {0, {false}} },
    {"1.64",
        "SELECT '%a_' LIKE 'ම%Aම_' COLLATE \"unicode\" ESCAPE 'ම';",
        {0, {false}} },
    {"1.65",
        "SELECT '%a_' COLLATE \"unicode\" LIKE 'ම%Aම_' COLLATE \"unicode\" ESCAPE 'ම' COLLATE \"unicode\";",
        {0, {false}} },
    {"1.66",
        "SELECT '%a_' LIKE 'ම%Aම_' ESCAPE 'ම' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.67",
        "SELECT '%_' LIKE 'a%a_' ESCAPE 'A' COLLATE \"unicode_ci\";",
        {0, {false}} },
    {"1.68",
        "SELECT '%_' LIKE 'a%a_' ESCAPE 'a' COLLATE \"unicode_ci\";",
        {0, {true}} },
    {"1.69",
        "SELECT 'Ї' COLLATE \"unicode\" LIKE 'ї' COLLATE \"unicode_uk_s3\";",
        {1, "Illegal mix of collations"} },
    {"1.70",
        "SELECT '%a_' COLLATE \"unicode_ci\" LIKE 'ම%Aම_' COLLATE \"unicode\" ESCAPE 'ම';",
        {1, "Illegal mix of collations"} },
    {"1.71",
        "SELECT '%a_' COLLATE \"unicode\" LIKE 'ම%Aම_' ESCAPE 'ම' COLLATE \"unicode_ci\";",
        {1, "Illegal mix of collations"} },
    {"1.72",
        "SELECT '%_' LIKE 'a%a_' COLLATE \"unicode\" ESCAPE 'A' COLLATE \"unicode_ci\";",
        {1, "Illegal mix of collations"} }
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
