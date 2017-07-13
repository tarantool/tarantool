#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(8)

-- box.cfg{wal_mode='none'}

table_count = 51

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

--    if not pcall(function() box.sql.execute(select_string) end) then
--        print('not ok')
--    end

--    select_string = select_string .. ' ' .. term ..' ' .. 'SELECT * FROM t' .. table_count
--    if pcall(function() box.sql.execute(select_string) end) then
--        print('not ok')
--    end
end

test:finish_test()
