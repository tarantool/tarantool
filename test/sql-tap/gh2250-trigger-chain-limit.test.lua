#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(8)

for _, table_count in ipairs({30, 31}) do
    -- Clean up, create tables, add entries
    for i = 1,table_count do
        -- First table for uniform triggers check
        drop_string = 'DROP TABLE IF EXISTS t' .. i .. ';'
        box.sql.execute(drop_string)

        create_string = 'CREATE TABLE t' .. i .. ' (s1 int primary key, s2 int);'
        box.sql.execute(create_string)

        insert_string = 'INSERT INTO t' .. i .. ' VALUES (0,' .. i .. ');'
        box.sql.execute(insert_string)

        -- Second table for triggers mixture check
        drop_string = 'DROP TABLE IF EXISTS tt' .. i .. ';'
        box.sql.execute(drop_string)

        create_string = 'CREATE TABLE tt' .. i .. ' (s1 int primary key, s2 int);'
        box.sql.execute(create_string)

        insert_string = 'INSERT INTO tt' .. i .. ' VALUES (0,' .. i .. ');'
        box.sql.execute(insert_string)
    end

    -- And ON DELETE|UPDATE|INSERT triggers
    for i = 1,table_count-1 do
        create_string = 'CREATE TRIGGER td' .. i
        create_string = create_string .. ' BEFORE DELETE ON t' .. i
            .. ' FOR EACH ROW '
        create_string = create_string .. ' BEGIN DELETE FROM t' .. i+1
            .. '; END'
        box.sql.execute(create_string)

        create_string = 'CREATE TRIGGER tu' .. i
        create_string = create_string .. ' BEFORE UPDATE ON t' .. i
            .. ' FOR EACH ROW '
        create_string = create_string .. ' BEGIN UPDATE t' .. i+1 ..
            ' SET s1=s1+1; END'
        box.sql.execute(create_string)

        create_string = 'CREATE TRIGGER ti' .. i
        create_string = create_string .. ' BEFORE INSERT ON t' .. i
            .. ' FOR EACH ROW '
        create_string = create_string .. ' BEGIN INSERT INTO t' .. i+1
            .. ' (s1) SELECT max(s1)+1 FROM t' .. i+1 .. '; END'
        box.sql.execute(create_string)

        -- Try triggers mixture: DELETE triggers UPDATE, which triggers
        -- INSERT, which triggers DELETE etc.
        create_string = 'CREATE TRIGGER tt' .. i
        if i % 3 == 0 then
            create_string = create_string .. ' BEFORE INSERT ON tt' .. i
                .. ' FOR EACH ROW'
            create_string = create_string .. ' BEGIN DELETE FROM tt' .. i+1
                .. '; END'
        else
            if (i - math.floor(i / 3)) % 2 == 0 then
                create_string = create_string .. ' BEFORE UPDATE ON tt' .. i
                    .. ' FOR EACH ROW'
                create_string = create_string .. ' BEGIN INSERT INTO tt' .. i+1
                    .. ' (s1) SELECT max(s1)+1 FROM tt' .. i+1 .. '; END'
            else
                create_string = create_string .. ' BEFORE DELETE ON tt' .. i
                    .. ' FOR EACH ROW'
                create_string = create_string .. ' BEGIN UPDATE tt' .. i+1 ..
                    ' SET s1=s1+1; END'
            end
        end
        box.sql.execute(create_string)
    end

    function check(sql)
        msg = ''
        local _, msg = pcall(function () test:execsql(sql) end)
        test:do_test(sql,
                     function()
                         return true
                     end,
                     table_count <= 30 or msg == 'Maximum number of chained trigger activations exceeded.')
    end

    -- Exceed check for UPDATE
    check('UPDATE t1 SET s1=2')

    -- Exceed check for INSERT
    check('INSERT INTO t1 (s1) VALUES (3)')

    -- Exceed check for DELETE
    check('DELETE FROM t1')

    -- Exceed check for triggers mixture
    check('DELETE FROM tt1')
end

test:finish_test()
