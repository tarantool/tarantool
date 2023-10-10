local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function()
    g.server:stop()
end)

g.test_create_table = function()
    g.server:exec(function()
        -- Make sure table, columns, indexes and constraint names are
        -- case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT, Second INT
                      CONSTRAINT field_CK CHECK (Second > 10)
                      CONSTRAINT field_FK REFERENCES Tab(First),
                      CONSTRAINT tuple_CK CHECK (Second > 10),
                      CONSTRAINT tuple_FK FOREIGN KEY (Second)
                      REFERENCES Tab(First),
                      CONSTRAINT Pk PRIMARY KEY (Second, First),
                      CONSTRAINT Uq UNIQUE (First, Second));]])
        t.assert(box.space.Tab ~= nil);
        t.assert_equals(box.space.Tab:format()[1].name, 'First');
        t.assert_equals(box.space.Tab:format()[2].name, 'Second');
        t.assert(box.space.Tab.foreign_key.tuple_FK ~= nil);
        t.assert(box.space.Tab.constraint.tuple_CK ~= nil);
        t.assert(box.space.Tab.index.Pk ~= nil);
        t.assert(box.space.Tab.index.Uq ~= nil);
        t.assert(box.space.Tab:format()[2].foreign_key.field_FK ~= nil);
        t.assert(box.space.Tab:format()[2].constraint.field_CK ~= nil);
        box.space.Tab:drop()
        box.func.check_Tab_field_CK:drop()
        box.func.check_Tab_tuple_CK:drop()
    end)
end

g.test_release_savepoint = function()
    g.server:exec(function()
        -- Make sure savepoint name in RELEASE is case-sensitive.
        local exp = {row_count = 0}
        t.assert_equals(box.execute([[START TRANSACTION;]]), exp)
        t.assert_equals(box.execute([[SAVEPOINT aSd;]]), exp)
        t.assert_equals(box.execute([[RELEASE aSd;]]), exp)
        t.assert_equals(box.execute([[ROLLBACK;]]), exp)

        -- Make sure savepoint name is looked up twice in RELEASE.
        t.assert_equals(box.execute([[START TRANSACTION;]]), exp)
        t.assert_equals(box.execute([[SAVEPOINT ASD;]]), exp)
        t.assert_equals(box.execute([[RELEASE aSd;]]), exp)
        t.assert_equals(box.execute([[ROLLBACK;]]), exp)
    end)
end

g.test_rollback_to_savepoint = function()
    g.server:exec(function()
        -- Make sure savepoint name in ROLLBACK TO is case-sensitive.
        local exp = {row_count = 0}
        t.assert_equals(box.execute([[START TRANSACTION;]]), exp)
        t.assert_equals(box.execute([[SAVEPOINT aSd;]]), exp)
        t.assert_equals(box.execute([[ROLLBACK TO aSd;]]), exp)
        t.assert_equals(box.execute([[ROLLBACK;]]), exp)

        -- Make sure savepoint name is looked up twice IN ROLLBACK TO.
        t.assert_equals(box.execute([[START TRANSACTION;]]), exp)
        t.assert_equals(box.execute([[SAVEPOINT ASD;]]), exp)
        t.assert_equals(box.execute([[ROLLBACK TO aSd;]]), exp)
        t.assert_equals(box.execute([[ROLLBACK;]]), exp)
    end)
end

g.test_collation_name = function()
    g.server:exec(function()
        -- Make sure collation names are case-sensitive.
        local map = setmetatable({}, { __serialize = 'map' })
        local coll_def = {'qwE', 1, 'BINARY', '', map}
        local coll = box.space._collation:auto_increment(coll_def)
        t.assert(coll ~= nil)
        t.assert_equals(coll.name, 'qwE')
        local sql = [[SELECT UPPER('asd' COLLATE qwE);]]
        t.assert_equals(box.execute(sql).rows, {{'ASD'}})
        box.space._collation:delete({coll.id})

        coll_def = {'ZXC', 1, 'BINARY', '', map}
        coll = box.space._collation:auto_increment(coll_def)
        t.assert(coll ~= nil)
        t.assert_equals(coll.name, 'ZXC')
        sql = [[SELECT UPPER('asd' COLLATE zXc);]]
        t.assert_equals(box.execute(sql).rows, {{'ASD'}})

        sql = [[CREATE TABLE t(s STRING PRIMARY KEY COLLATE Zxc);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        t.assert_equals(box.space.t:format()[1].collation, coll.id)
        box.space.t:drop()
        box.space._collation:delete({coll.id})
    end)
end

g.test_create_tuple_foreign_key = function()
    g.server:exec(function()
        -- Make sure table name and column names in foreign key definition are
        -- case-sensitive.
        box.execute([[CREATE TABLE tab(first INT PRIMARY KEY, second INT,
                      CONSTRAINT one FOREIGN KEY (first)
                      REFERENCES tab(second));]])
        t.assert(box.space.tab.foreign_key.one ~= nil);
        box.space.tab:drop()
    end)
end

g.test_create_field_foreign_key = function()
    g.server:exec(function()
        -- Make sure table name and foreign column name in foreign key
        -- definition are case-sensitive.
        box.execute([[CREATE TABLE tab(first INT PRIMARY KEY, second INT
                      CONSTRAINT one REFERENCES tab(second));]])
        t.assert(box.space.tab:format()[2].foreign_key.one ~= nil);
        box.space.tab:drop()
    end)
end

g.test_expression = function()
    g.server:exec(function()
        -- Make sure column names and function names in expressions are
        -- case-sensitive.
        box.schema.func.create("fUn", {returns = 'number',
                               body = 'function (a, b) return a + b end',
                               param_list = {'number', 'number'},
                               exports = {'LUA', 'SQL'}})
        box.execute([[CREATE TABLE Tab(fiRst INT PRIMARY KEY, seconD INT);]])
        box.execute([[INSERT INTO Tab VALUES(1, 123);]])
        local sql = [[SELECT fUn(fiRst, seconD) FROM Tab;]]
        t.assert_equals(box.execute(sql).rows, {{124}})
        box.space.Tab:drop()
        box.func.fUn:drop()
    end)
end

g.test_create_primary_key = function()
    g.server:exec(function()
        -- Make sure column names in primary key definition are case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT, Second INT,
                      CONSTRAINT Three PRIMARY KEY (Second, First));]])
        t.assert_equals(box.space.Tab.index[0].name, 'Three');
        box.space.Tab:drop()
    end)
end

g.test_create_unique = function()
    g.server:exec(function()
        -- Make sure column names in unique constraint definition are
        -- case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY, Second INT,
                      CONSTRAINT Four UNIQUE (Second, First));]])
        t.assert_equals(box.space.Tab.index[1].name, 'Four');
        box.space.Tab:drop()
    end)
end

g.test_drop_table = function()
    g.server:exec(function()
        -- Make sure table name in DROP TABLE is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY);]])
        t.assert(box.space.Tab ~= nil);
        box.execute([[DROP TABLE Tab;]])
        t.assert(box.space.Tab == nil);
    end)
end

g.test_select_from = function()
    g.server:exec(function()
        -- Make sure table name in SELECT FROM is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY);]])
        box.space.Tab:insert({123})
        t.assert_equals(box.execute([[SELECT * FROM Tab;]]).rows, {{123}});
        box.space.Tab:drop()
    end)
end

g.test_drop_view = function()
    g.server:exec(function()
        -- Make sure view names in DROP VIEW is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY);]])
        box.execute([[CREATE VIEW Vw AS SELECT * from Tab;]])
        t.assert(box.space.Vw ~= nil);
        box.execute([[DROP VIEW Vw;]])
        t.assert(box.space.Vw == nil);
        box.space.Tab:drop()
    end)
end

g.test_indexed_by = function()
    g.server:exec(function()
        -- Make sure index name in INDEXED BY is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY, Second INT,
                      CONSTRAINT Four UNIQUE (Second, First));]])
        local _, err = box.execute([[SELECT * FROM Tab INDEXED BY Four;]])
        t.assert(err == nil);
        box.space.Tab:drop()
    end)
end

g.test_delete_from = function()
    g.server:exec(function()
        -- Make sure table name in DELETE FROM is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY);]])
        box.space.Tab:insert({123})
        t.assert_equals(box.space.Tab:count(), 1)
        t.assert_equals(box.execute([[DELETE FROM Tab;]]), {row_count = 1});
        t.assert_equals(box.space.Tab:count(), 0)
        box.space.Tab:drop()
    end)
end

g.test_truncate = function()
    g.server:exec(function()
        -- Make sure table name in TRUNCATE is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY);]])
        box.space.Tab:insert({123})
        t.assert_equals(box.space.Tab:count(), 1)
        t.assert_equals(box.execute([[TRUNCATE TABLE Tab;]]), {row_count = 0});
        t.assert_equals(box.space.Tab:count(), 0)
        box.space.Tab:drop()
    end)
end

g.test_update = function()
    g.server:exec(function()
        -- Make sure table name and column names in UPDATE are case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY, Qwe INT, Zxc INT);]])
        box.space.Tab:insert({3, 5, 7})
        local sql = [[UPDATE Tab SET Qwe = 1, Zxc = 9;]]
        t.assert_equals(box.execute(sql), {row_count = 1});
        t.assert_equals(box.space.Tab:select(), {{3, 1, 9}})
        sql = [[UPDATE Tab SET (Qwe, Zxc) = (11, -1);]]
        t.assert_equals(box.execute(sql), {row_count = 1});
        t.assert_equals(box.space.Tab:select(), {{3, 11, -1}})
        box.space.Tab:drop()
    end)
end

g.test_insert = function()
    g.server:exec(function()
        -- Make sure table name and column names in INSERT are case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY, Qwe INT, Zxc INT);]])
        box.execute([[INSERT INTO Tab(Pk, Zxc) VALUES (123, 321);]])
        t.assert_equals(box.space.Tab:select(), {{123, nil, 321}})
        box.space.Tab:drop()
    end)
end

g.test_create_index = function()
    g.server:exec(function()
        -- Make sure table name and column names in CREATE INDEX are
        -- case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY, Qwe INT, Zxc INT);]])
        local sql = [[CREATE INDEX In1 ON Tab(Zxc, Qwe);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        t.assert(box.space.Tab.index.In1 ~= nil)
        box.space.Tab:drop()
    end)
end

g.test_drop_index = function()
    g.server:exec(function()
        -- Make sure table name and index name in DROP INDEX are case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY, Qwe INT, Zxc INT);]])
        box.execute([[CREATE INDEX In1 ON Tab(Zxc, Qwe);]])
        t.assert(box.space.Tab.index.In1 ~= nil)
        local sql = [[DROP INDEX In1 ON Tab;]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        t.assert(box.space.Tab.index.In1 == nil)
        box.space.Tab:drop()
    end)
end

g.test_pragma = function()
    g.server:exec(function()
        -- Make sure table name and index name in PRAGMA are case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY, Qwe INT, Zxc INT);]])
        box.execute([[CREATE INDEX In1 ON Tab(Zxc, Qwe);]])
        local exp = {{0, 2, 'Zxc', 0, 'BINARY', 'integer'},
                     {1, 1, 'Qwe', 0, 'BINARY', 'integer'}}
        t.assert_equals(box.execute([[PRAGMA index_info(Tab.In1);]]).rows, exp)
        box.space.Tab:drop()
    end)
end

g.test_show_create_table = function()
    g.server:exec(function()
        -- Make sure table name in SHOW CREATE TABLE is case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT CONSTRAINT One PRIMARY KEY);]])
        local _, err = box.execute([[SHOW CREATE TABLE Tab;]])
        t.assert(err == nil)
        box.space.Tab:drop()
    end)
end

g.test_create_trigger = function()
    g.server:exec(function()
        -- Make sure table name in CREATE TRIGGER is case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY);]])
        local sql = [[CREATE TRIGGER Tr1 AFTER INSERT ON Tab FOR EACH ROW
                      BEGIN SELECT 1; END;]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        t.assert(box.space._trigger:get{'Tr1'} ~= nil)
        box.space.Tab:drop()
    end)
end

g.test_drop_trigger = function()
    g.server:exec(function()
        -- Make sure trigger name in DROP TRIGGER is case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY);]])
        box.execute([[CREATE TRIGGER Tr1 AFTER INSERT ON Tab FOR EACH ROW
                      BEGIN SELECT 1; END;]])
        t.assert(box.space._trigger:get{'Tr1'} ~= nil)
        t.assert_equals(box.execute([[DROP TRIGGER Tr1;]]), {row_count = 1})
        t.assert(box.space._trigger:get{'Tr1'} == nil)
        box.space.Tab:drop()
    end)
end

g.test_rename = function()
    g.server:exec(function()
        -- Make sure table name in RENAME is case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY);]])
        t.assert(box.space.Tab ~= nil)
        t.assert(box.space.Bat == nil)
        box.execute([[ALTER TABLE Tab RENAME TO Bat;]])
        t.assert(box.space.Tab == nil)
        t.assert(box.space.Bat ~= nil)
        box.space.Bat:drop()
    end)
end

g.test_add_column = function()
    g.server:exec(function()
        -- Make sure table name in ADD COLUMN is case-sensitive.
        box.execute([[CREATE TABLE Tab(Pk INT PRIMARY KEY);]])
        t.assert_equals(#box.space.Tab:format(), 1)
        box.execute([[ALTER TABLE Tab ADD COLUMN Two INT;]])
        t.assert_equals(#box.space.Tab:format(), 2)
        t.assert_equals(box.space.Tab:format()[2].name, 'Two')
        box.space.Tab:drop()
    end)
end

g.test_add_constraint = function()
    g.server:exec(function()
        -- Make sure table name in ADD CONSTRAINT is case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY, Second INT);]])
        box.space.Tab.index[0]:drop()
        t.assert(box.space.Tab.index[0] == nil)
        t.assert(box.space.Tab.constraint == nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT One PRIMARY KEY (First);]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert(box.space.Tab.index[1] == nil)
        t.assert(box.space.Tab.constraint == nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT Two UNIQUE (Second);]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert_equals(box.space.Tab.index[1].name, 'Two')
        t.assert(box.space.Tab.constraint == nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT Three CHECK (First < 5);]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert_equals(box.space.Tab.index[1].name, 'Two')
        t.assert(box.space.Tab.constraint.Three ~= nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT Four FOREIGN KEY (Second)
                      REFERENCES Tab(First);]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert_equals(box.space.Tab.index[1].name, 'Two')
        t.assert(box.space.Tab.constraint.Three ~= nil)
        t.assert(box.space.Tab.foreign_key.Four ~= nil)
        box.space.Tab:drop()
        box.func.check_Tab_Three:drop()
    end)
end

g.test_drop_constraint = function()
    g.server:exec(function()
        -- Make sure table name and constraint name in DROP CONSTRAINT is
        -- case-sensitive.
        box.execute([[CREATE TABLE Tab(First INT PRIMARY KEY, Second INT);]])
        box.space.Tab.index[0]:drop()
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT One PRIMARY KEY (First);]])
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT Two UNIQUE (Second);]])
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT Three CHECK (First < 5);]])
        box.execute([[ALTER TABLE Tab ADD CONSTRAINT Four FOREIGN KEY (Second)
                      REFERENCES Tab(First);]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert_equals(box.space.Tab.index[1].name, 'Two')
        t.assert(box.space.Tab.constraint.Three ~= nil)
        t.assert(box.space.Tab.foreign_key.Four ~= nil)

        box.execute([[ALTER TABLE Tab DROP CONSTRAINT Four;]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert_equals(box.space.Tab.index[1].name, 'Two')
        t.assert(box.space.Tab.constraint.Three ~= nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab DROP CONSTRAINT Three;]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert_equals(box.space.Tab.index[1].name, 'Two')
        t.assert(box.space.Tab.constraint == nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab DROP CONSTRAINT Two;]])
        t.assert_equals(box.space.Tab.index[0].name, 'One')
        t.assert(box.space.Tab.index[1] == nil)
        t.assert(box.space.Tab.constraint == nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.execute([[ALTER TABLE Tab DROP CONSTRAINT One;]])
        t.assert(box.space.Tab.index[0] == nil)
        t.assert(box.space.Tab.index[1] == nil)
        t.assert(box.space.Tab.constraint == nil)
        t.assert(box.space.Tab.foreign_key == nil)
        box.space.Tab:drop()
        box.func.check_Tab_Three:drop()
    end)
end
