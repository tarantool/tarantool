local server = require('luatest.server')
local t = require('luatest')

local g = t.group("drop_table", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_drop_table = function(cg)
    cg.server:exec(function()
        -- create space.
        local sql = "CREATE TABLE zzzoobar "..
                    "(c1 INT, c2 INT PRIMARY KEY, c3 TEXT, c4 INT);"
        box.execute(sql)

        box.execute("CREATE INDEX zb ON zzzoobar(c1, c3);")

        -- Dummy entry.
        box.execute("INSERT INTO zzzoobar VALUES (111, 222, 'c3', 444);")

        box.execute("DROP TABLE zzzoobar")

        -- Table does not exist anymore. Should error here.
        local exp_err = "Space 'zzzoobar' does not exist"
        sql = "INSERT INTO zzzoobar VALUES (111, 222, 'c3', 444);"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end

-- gh-3712: if space features sequence, data from _sequence_data
-- must be deleted before space is dropped.
g.test_3712_delete_before_space_drop = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1 (id INT PRIMARY KEY AUTOINCREMENT);")
        box.execute("INSERT INTO t1 VALUES (NULL);")
        local res = box.snapshot()
        t.assert_equals(res, 'ok')
    end)
    cg.server:restart()
end

-- gh-3592: clean-up garbage on failed CREATE TABLE statement.
g.test_3592_clean_up_if_failes_create_table = function(cg)
    cg.server:exec(function()
        box.execute("DROP TABLE t1;")

        -- Cleanup.
        -- DROP TABLE should do the job.

        -- Let user have enough rights to create space, but not enough to
        -- create index.
        --
        box.schema.user.create('tmp')
        box.schema.user.grant('tmp', 'create, read', 'universe')
        box.schema.user.grant('tmp', 'write', 'space', '_space')
        box.schema.user.grant('tmp', 'write', 'space', '_schema')

        -- Number of records in _space, _index, _sequence:
        local space_count = #box.space._space:select()
        local index_count = #box.space._index:select()
        local sequence_count = #box.space._sequence:select()

        box.session.su('tmp')
        --
        -- Error: user do not have rights to write in box.space._index.
        -- Space that was already created should be automatically dropped.
        --
        local exp_err = "Write access to space '_index' "..
                        "is denied for user 'tmp'"
        local sql = "CREATE TABLE t1 (id INT PRIMARY KEY, a INT);"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        -- Error: no such table.
        exp_err = "Space 't1' does not exist"
        _, err = box.execute('DROP TABLE t1;')
        t.assert_equals(tostring(err), exp_err)

        box.session.su('admin')

        --
        -- Check that _space, _index and _sequence have the same number of
        -- records.
        --
        t.assert_equals(space_count, #box.space._space:select())
        t.assert_equals(index_count, #box.space._index:select())
        t.assert_equals(sequence_count, #box.space._sequence:select())

        --
        -- Give user right to write in _index. Still have not enough
        -- rights to write in _sequence.
        --
        box.schema.user.grant('tmp', 'write', 'space', '_index')
        box.session.su('tmp')

        --
        -- Error: user do not have rights to write in _sequence.
        --
        exp_err = "Write access to space '_sequence' is denied for user 'tmp'"
        sql = "CREATE TABLE t2 (id INT PRIMARY KEY AUTOINCREMENT, "..
              "a INT UNIQUE, b INT UNIQUE, c INT UNIQUE, d INT UNIQUE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        box.session.su('admin')

        --
        -- Check that _space, _index and _sequence have the same number of
        -- records.
        --
        t.assert_equals(space_count, #box.space._space:select())
        t.assert_equals(index_count, #box.space._index:select())
        t.assert_equals(sequence_count, #box.space._sequence:select())

        local fk_constraint_count = #box.space._fk_constraint:select()

        --
        -- Check that clean-up works fine after another error.
        --
        box.schema.user.grant('tmp', 'write', 'space')
        box.session.su('tmp')

        box.execute('CREATE TABLE t3(a INTEGER PRIMARY KEY);')
        --
        -- Error: Failed to drop referenced table.
        --
        sql = 'CREATE TABLE t4(x INTEGER PRIMARY KEY REFERENCES t3, '..
              'a INT UNIQUE, c TEXT REFERENCES t3);'
        box.execute(sql)

        exp_err = "Can't modify space 't3': space is referenced by foreign key"
        _, err = box.execute('DROP TABLE t3;')
        t.assert_equals(tostring(err), exp_err)

        box.execute('DROP TABLE t4;')
        box.execute('DROP TABLE t3;')

        --
        -- Check that _space, _index and _sequence have the same number of
        -- records.
        --
        t.assert_equals(space_count, #box.space._space:select())
        t.assert_equals(index_count, #box.space._index:select())
        t.assert_equals(sequence_count, #box.space._sequence:select())
        t.assert_equals(fk_constraint_count, #box.space._fk_constraint:select())

        box.session.su('admin')

        box.schema.user.drop('tmp')
    end)
end
