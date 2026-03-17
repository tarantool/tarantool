local server = require('luatest.server')
local t = require('luatest')

local g = t.group("engine")

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check errors during function create process
g.test_engine = function(cg)
    cg.server:exec(function()
        box.execute([[SET SESSION "sql_default_engine" = 'vinyl';]])

        box.execute("CREATE TABLE t1_vinyl(a INT PRIMARY KEY, b INT, c INT);")
        box.execute("CREATE TABLE t2_vinyl(a INT PRIMARY KEY, b INT, c INT);")

        box.execute([[SET SESSION "sql_default_engine" = 'memtx';]])

        box.execute("CREATE TABLE t3_memtx(a INT PRIMARY KEY, b INT, c INT);")

        t.assert_equals(box.space.t1_vinyl.engine, 'vinyl')
        t.assert_equals(box.space.t2_vinyl.engine, 'vinyl')
        t.assert_equals(box.space.t3_memtx.engine, 'memtx')

        box.execute("DROP TABLE t1_vinyl;")
        box.execute("DROP TABLE t2_vinyl;")
        box.execute("DROP TABLE t3_memtx;")
    end)
end

-- gh-4422: allow to specify engine in CREATE TABLE statement.
g.test_4422_allow_specify_engine_in_create_table = function(cg)
    cg.server:exec(function()
        local sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                      WITH ENGINE = 'vinyl';]]
        box.execute(sql)
        t.assert_equals(box.space.t1_vinyl.engine, 'vinyl')
        sql = [[CREATE TABLE t1_memtx (id INT PRIMARY KEY)
                WITH ENGINE = 'memtx';]]
        box.execute(sql)
        t.assert_equals(box.space.t1_memtx.engine, 'memtx')

        box.execute([[SET SESSION "sql_default_engine" = 'vinyl';]])

        sql = [[CREATE TABLE t2_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = 'vinyl';]]
        box.execute(sql)
        t.assert_equals(box.space.t2_vinyl.engine, 'vinyl')
        sql = [[CREATE TABLE t2_memtx (id INT PRIMARY KEY)
                WITH ENGINE = 'memtx';]]
        box.execute(sql)
        t.assert_equals(box.space.t2_memtx.engine, 'memtx')

        box.space.t1_vinyl:drop()
        box.space.t1_memtx:drop()
        box.space.t2_vinyl:drop()
        box.space.t2_memtx:drop()

        -- Name of engine considered to be string literal, so should be
        -- lowercased and quoted.
        local exp_err = "Syntax error at line 2 near 'VINYL'"
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                      WITH ENGINE = VINYL;]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Syntax error at line 2 near 'vinyl'"
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                      WITH ENGINE = vinyl;]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Space engine 'VINYL' does not exist"
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = 'VINYL';]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Syntax error at line 2 near '"vinyl"']]
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = "vinyl";]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        -- Make sure that wrong engine name is handled properly.
        exp_err = "Space engine 'abc' does not exist"
        sql = [[CREATE TABLE t_wrong_engine (id INT PRIMARY KEY)
                WITH ENGINE = 'abc';]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Failed to create space 't_long_engine_name': "..
                  "space engine name is too long"
        sql = [[CREATE TABLE t_long_engine_name (id INT PRIMARY KEY)
                WITH ENGINE = 'very_long_engine_name';]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end

--
-- gh-4183: Check if there is a garbage in case of failure to
-- create a constraint, when more than one constraint of the same
-- type is created with the same name and in the same
-- CREATE TABLE statement.
--
g.test_duplicate_function = function(cg)
    cg.server:exec(function()
        local _, err = box.execute([[CREATE TABLE t1(id INT PRIMARY KEY,
                                     CONSTRAINT ck1 CHECK(id > 0),
                                     CONSTRAINT ck1 CHECK(id < 0));]])
        t.assert_equals(err.message, "Function 'check_t1_ck1' already exists")
        t.assert_equals(box.space.t1, nil)
    end)
end

g = t.group("drop_table", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- gh-3592: clean-up garbage on failed CREATE TABLE statement.
g.test_3592_clean_up_if_failed_create_table = function(cg)
    cg.server:exec(function()
        -- Let user have enough rights to create space, but not enough to
        -- create index.
        --
        box.schema.user.create('tmp')
        box.schema.user.grant('tmp', 'create, read', 'universe')
        box.schema.user.grant('tmp', 'write', 'space', '_space')
        box.schema.user.grant('tmp', 'write', 'space', '_schema')

        -- Number of records in _space, _index, _sequence:
        local space_count = box.space._space:count()
        local index_count = box.space._index:count()
        local sequence_count = box.space._sequence:count()

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

        box.session.su('guest')

        --
        -- Check that _space, _index and _sequence have the same number of
        -- records.
        --
        t.assert_equals(space_count, box.space._space:count())
        t.assert_equals(index_count, box.space._index:count())
        t.assert_equals(sequence_count, box.space._sequence:count())

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

        box.session.su('guest')

        --
        -- Check that _space, _index and _sequence have the same number of
        -- records.
        --
        t.assert_equals(space_count, box.space._space:count())
        t.assert_equals(index_count, box.space._index:count())
        t.assert_equals(sequence_count, box.space._sequence:count())

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
        t.assert_equals(space_count, box.space._space:count())
        t.assert_equals(index_count, box.space._index:count())
        t.assert_equals(sequence_count, box.space._sequence:count())

        box.session.su('guest')

        box.schema.user.drop('tmp')
    end)
end
