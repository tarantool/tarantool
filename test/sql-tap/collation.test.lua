#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(162)

local prefix = "collation-"

test:do_test(
    prefix.."0.1",
    function ()
        box.internal.collation.create('unicode_numeric', 'ICU', 'ru-RU', {numeric_collation="ON"})
        box.internal.collation.create('unicode_numeric_s2', 'ICU', 'ru-RU', {numeric_collation="ON", strength="secondary"})
        box.internal.collation.create('unicode_tur_s2', 'ICU', 'tu', {strength="secondary"})
    end
)

test:do_execsql_test(
    prefix.."0.2",
    "pragma collation_list",
    {
        0,"unicode",
        1,"unicode_ci",
        2,"unicode_numeric",
        3,"unicode_numeric_s2",
        4,"unicode_tur_s2"
    }
)

-- we suppose that tables are immutable
local function merge_tables(...)
    local r = {}
    local N = select('#', ...)
    for i = 1, N do
        local tbl = select(i, ...)
        for _, row in ipairs(tbl) do
            table.insert(r, row)
        end
    end
    return r
end

local function insert_into_table(tbl_name, data)
    local sql = string.format([[ INSERT INTO %s VALUES ]], tbl_name)
    local values = {}
    for _, row in ipairs(data) do
        local items = {}
        for _, item in ipairs(row) do
            if type(item) == "string" then
                table.insert(items, "'"..item.."'")
            end
            if type(item) == "number" then
                table.insert(items, item)
            end
        end
        local value = "("..table.concat(items, ",")..")"
        table.insert(values, value)
    end
    values = table.concat(values, ",")
    sql = sql..values
    test:execsql(sql)
end

local data_eng = {
    {1, "Aa"},
    {2, "a"},
    {3, "aa"},
    {4, "ab"},
    {5, "Ac"},
    {6, "Ad"},
    {7, "AD"},
    {8, "aE"},
    {9, "ae"},
    {10, "aba"},
}
local data_num = {
    {21, "1"},
    {22, "2"},
    {23, "21"},
    {24, "23"},
    {25, "3"},
    {26, "9"},
    {27, "0"},
}
local data_symbols = {
    {41, " "},
    {42, "!"},
    {43, ")"},
    {44, "/"},
    {45, ":"},
    {46, "<"},
    {47, "@"},
    {48, "["},
    {49, "`"},
    {50, "}"},
}
local data_ru = {
    -- Russian strings
    {61, "А"},
    {62, "а"},
    {63, "Б"},
    {64, "б"},
    {65, "е"},
    {66, "её"},
    {67, "Её"},
    {68, "ЕЁ"},
    {69, "еЁ"},
    {70, "ёёё"},
    {71, "ё"},
    {72, "Ё"},
    {73, "ж"},
}

local data_combined = merge_tables(data_eng, data_num, data_symbols, data_ru)

--------------------------------------------
-----TEST CASES FOR DIFFERENT COLLATIONS----
--------------------------------------------

local data_test_binary_1 = {
    --   test_name , data to fill with, result output in col
    {"en", data_eng, {"AD","Aa","Ac","Ad","a","aE","aa","ab","aba","ae"}},
    {"num", data_num, {"0","1","2","21","23","3","9"}},
    {"symbols", data_symbols, {" ","!",")","/",":","<","@","[","`","}"}},
    {"ru", data_ru, {"Ё","А","Б","ЕЁ","Её","а","б","е","еЁ","её","ж","ё","ёёё"}},
    {"combined", data_combined,
        {" ","!",")","/","0","1","2","21","23","3","9",":","<","@",
            "AD","Aa","Ac","Ad","[","`","a","aE","aa","ab","aba","ae",
            "}","Ё","А","Б","ЕЁ","Её","а","б","е","еЁ","её","ж","ё","ёёё"}}
}

local data_test_unicode = {
    --   test_name , data to fill with, result output in col
    {"en", data_eng, {"a","aa","Aa","ab","aba","Ac","Ad","AD","ae","aE"}},
    {"num", data_num, {"0","1","2","21","23","3","9"}},
    {"symbols", data_symbols, {" ",":","!",")","[","}","@","/","`","<"}},
    {"ru", data_ru, {"а","А","б","Б","е","ё","Ё","её","еЁ","Её","ЕЁ","ёёё","ж"}},
    {"combined", data_combined,
        {" ",":","!",")","[","}","@","/","`","<","0","1","2","21","23",
            "3","9","a","aa","Aa","ab","aba","Ac","Ad","AD","ae","aE","а",
            "А","б","Б","е","ё","Ё","её","еЁ","Её","ЕЁ","ёёё","ж"}}
}


local data_test_unicode_ci = {
    --   test_name , data to fill with, result output in col
    {"en", data_eng, {"a","Aa","aa","ab","aba","Ac","Ad","AD","aE","ae"}},
    {"num", data_num, {"0","1","2","21","23","3","9"}},
    {"symbols", data_symbols, {" ",":","!",")","[","}","@","/","`","<"}},
    {"ru", data_ru, {"А","а","Б","б","е","ё","Ё","её","Её","ЕЁ","еЁ","ёёё","ж"}},
    {"combined", data_combined,
        {" ",":","!",")","[","}","@","/","`","<","0","1","2","21","23",
            "3","9","a","aa","Aa","ab","aba","Ac","Ad","AD","ae","aE","а",
            "А","б","Б","е","ё","Ё","её","еЁ","Её","ЕЁ","ёёё","ж"}}
}

local data_collations = {
    -- default collation = binary
    {"/*COLLATE DEFAULT*/", data_test_binary_1},
    {"COLLATE BINARY", data_test_binary_1},
    {"COLLATE \"unicode\"", data_test_unicode},
    {"COLLATE \"unicode_ci\"", data_test_unicode_ci},
}

for _, data_collation in ipairs(data_collations) do
    for _, test_case in ipairs(data_collation[2]) do
        local extendex_prefix = string.format("%s1.%s.%s.", prefix, data_collation[1], test_case[1])
        local data = test_case[2]
        local result = test_case[3]
        test:do_execsql_test(
            extendex_prefix.."create_table",
            string.format("create table t1(a primary key, b %s);", data_collation[1]),
            {})
        test:do_test(
            extendex_prefix.."insert_values",
            function()
                return insert_into_table("t1", data)
            end, {})
        test:do_execsql_test(
            extendex_prefix.."select_plan_contains_b-tree",
            string.format("explain query plan select b from t1 order by b %s;",data_collation[1]),
            {0,0,0,"SCAN TABLE T1",
                0,0,0,"USE TEMP B-TREE FOR ORDER BY"})
        test:do_execsql_test(
            extendex_prefix.."select",
            string.format("select b from t1 order by b %s;",data_collation[1]),
            result)
        test:do_execsql_test(
            extendex_prefix.."create index",
            string.format("create index i on t1(b %s)",data_collation[1]),
            {})
        test:do_execsql_test(
            extendex_prefix.."select_from_index_plan_does_not_contain_b-tree",
            string.format("explain query plan select b from t1 order by b %s;",data_collation[1]),
            {0,0,0,"SCAN TABLE T1 USING COVERING INDEX I"})
        test:do_execsql_test(
            extendex_prefix.."select_from_index",
            string.format("select b from t1 order by b %s;",data_collation[1]),
            result)
        test:do_execsql_test(
            extendex_prefix.."drop_table",
            "drop table t1",
            {})
    end
end

test:finish_test()
