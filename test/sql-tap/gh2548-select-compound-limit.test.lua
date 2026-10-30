#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(9)

-- box.cfg{wal_mode='none'}

local table_count = 31

local select_string_last = ''

for _, term in ipairs({'UNION', 'UNION ALL', 'INTERSECT', 'EXCEPT'}) do
    local select_string = ''
    test:do_test("Positive COMPOUND "..term,
                 function()
                     for i = 1,table_count do
                         local drop_string = 'DROP TABLE IF EXISTS t' .. i .. ';\n'
                         test:execsql(drop_string)
                     end

                     for i = 1,table_count do
                         local create_string = 'CREATE TABLE t' .. i .. ' (s1 int primary key, s2 int);\n'
                         test:execsql(create_string)
                     end

                     for i = 1,table_count do
                         local insert_string = 'INSERT INTO t' .. i .. ' VALUES (0,' .. i .. ');\n'
                         test:execsql(insert_string)
                     end

                     for i = 1,table_count-1 do
                         if i > 1 then select_string = select_string .. ' ' .. term .. ' ' end
                         select_string = select_string .. 'SELECT * FROM t' .. i
                     end
                     return pcall( function() test:execsql(select_string) end)
                 end,
                 true)
    test:do_test("Negative COMPOUND "..term,
                 function()
                     select_string = select_string .. ' ' .. term ..' ' .. 'SELECT * FROM t' .. table_count
                     return  pcall(function() test:execsql(select_string) end)
                 end,
                 false)

    select_string_last = select_string

--    if not pcall(function() box.execute(select_string) end) then
--        print('not ok')
--    end

--    select_string = select_string .. ' ' .. term ..' ' .. 'SELECT * FROM t' .. table_count
--    if pcall(function() box.execute(select_string) end) then
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

test:finish_test()
