#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(14)

-- box.cfg{wal_mode='none'}

table_count = 31

select_string_last = ''

for _, term in ipairs({'UNION', 'UNION ALL', 'INTERSECT', 'EXCEPT'}) do
    select_string = ''
    test:do_test("Positive COMPOUND "..term,
                 function()
                     for i = 1,table_count do
                         drop_string = 'DROP TABLE IF EXISTS t' .. i .. ';\n'
                         box.sql.execute(drop_string)
                     end

                     for i = 1,table_count do
                         create_string = 'CREATE TABLE t' .. i .. ' (s1 int primary key, s2 int);\n'
                         box.sql.execute(create_string)
                     end

                     for i = 1,table_count do
                         insert_string = 'INSERT INTO t' .. i .. ' VALUES (0,' .. i .. ');\n'
                         box.sql.execute(insert_string)
                     end

                     for i = 1,table_count-1 do
                         if i > 1 then select_string = select_string .. ' ' .. term .. ' ' end
                         select_string = select_string .. 'SELECT * FROM t' .. i
                     end
                     return pcall( function() box.sql.execute(select_string) end)
                 end,
                 true)
    test:do_test("Negative COMPOUND "..term,
                 function()
                     select_string = select_string .. ' ' .. term ..' ' .. 'SELECT * FROM t' .. table_count
                     return  pcall(function() box.sql.execute(select_string) end)
                 end,
                 false)

    select_string_last = select_string

--    if not pcall(function() box.sql.execute(select_string) end) then
--        print('not ok')
--    end

--    select_string = select_string .. ' ' .. term ..' ' .. 'SELECT * FROM t' .. table_count
--    if pcall(function() box.sql.execute(select_string) end) then
--        print('not ok')
--    end
end


test:do_catchsql_test(
    "gh2548-select-compound-limit-2",
    select_string_last, {
        -- <gh2548-select-compound-limit-2>
        1, "The number of UNION or EXCEPT or INTERSECT operations 31 exceeds the limit (30)"
        -- </gh2548-select-compound-limit-2>
    })

test:do_execsql_test(
    "gh2548-select-compound-limit-3.1", [[
        pragma sql_compound_select_limit
    ]], {
        -- <gh2548-select-compound-limit-3.1>
        30
        -- </gh2548-select-compound-limit-3.1>
    })

test:do_execsql_test(
    "gh2548-select-compound-limit-3.2", [[
        pragma sql_compound_select_limit=31
    ]], {
        -- <gh2548-select-compound-limit-3.2>
        31
        -- </gh2548-select-compound-limit-3.2>
})

test:do_execsql_test(
    "gh2548-select-compound-limit-3.3",
    select_string_last, {
        -- <gh2548-select-compound-limit-3.3>
        0, 1
        -- </gh2548-select-compound-limit-3.3>
    })

test:do_execsql_test(
    "gh2548-select-compound-limit-3.4", [[
        pragma sql_compound_select_limit=0
    ]], {
        -- <gh2548-select-compound-limit-3.4>
        0
        -- </gh2548-select-compound-limit-3.4>
    })

test:do_execsql_test(
    "gh2548-select-compound-limit-3.3",
    select_string_last, {
        -- <gh2548-select-compound-limit-3.3>
        0, 1
        -- </gh2548-select-compound-limit-3.3>
    })

test:finish_test()
