local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        rawset(_G, 'check', function(table_raw_name, res, err)
            local sql = 'SHOW CREATE TABLE '..table_raw_name..';'
            local ret = box.execute(sql)
            t.assert_equals(ret.rows[1][1], res)
            t.assert_equals(ret.rows[1][2], err)
        end)
    end)
end)

g.after_all(function()
    g.server:stop()
end)

g.test_show_create_table_one = function()
    g.server:exec(function()
        local _, err = box.execute('SHOW CREATE TABLE t;')
        t.assert_equals(err.message, [[Space 't' does not exist]])

        box.execute('CREATE TABLE t(i INT PRIMARY KEY, a INT);')
        local res = {'CREATE TABLE t(\ni INTEGER NOT NULL,\na INTEGER,\n'..
                     'CONSTRAINT pk_unnamed_t_1 PRIMARY KEY(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('t', res)
        box.execute('DROP TABLE t;')

        local sql = [[CREATE TABLE t(i INT CONSTRAINT c0 PRIMARY KEY,
                                     a STRING CONSTRAINT c1 REFERENCES t(i)
                                     CONSTRAINT c2 UNIQUE,
                                     b UUID NOT NULL,
                                     CONSTRAINT c3 CHECK(i * a < 100),
                                     CONSTRAINT c4 UNIQUE (a, b),
                                     CONSTRAINT c5 FOREIGN KEY(i, a)
                                     REFERENCES t(a, b))
                      WITH ENGINE = 'vinyl';]]
        box.execute(sql)
        res = {'CREATE TABLE t(\ni INTEGER NOT NULL,\n'..
               'a STRING CONSTRAINT c1 REFERENCES t(i),\n'..
               'b UUID NOT NULL,\n'..
               'CONSTRAINT c0 PRIMARY KEY(i),\n'..
               'CONSTRAINT c3 CHECK(i * a < 100),\n'..
               'CONSTRAINT c5 FOREIGN KEY(i, a) REFERENCES t(a, b))\n'..
               "WITH ENGINE = 'vinyl';",
               'CREATE UNIQUE INDEX c2 ON t(a);',
               'CREATE UNIQUE INDEX c4 ON t(a, b);'}
        _G.check('t', res)
        box.execute('DROP TABLE t;')

        -- Make sure SHOW, INCLUDING and ERRORS can be used as names.
        sql = [[CREATE TABLE show(a INT PRIMARY KEY, b INT);]]
        local ret = box.execute(sql)
        t.assert(ret ~= nil);
        box.execute([[DROP TABLE show;]])
    end)
end

g.test_show_create_table_all = function()
    g.server:exec(function()
        local res = box.execute('SHOW CREATE TABLE;')
        t.assert_equals(res.rows, {})

        -- Make sure that a description of all non-system spaces is displayed.
        box.execute('CREATE TABLE t1(i INT PRIMARY KEY, a INT);')
        box.execute('CREATE TABLE t2(i INT PRIMARY KEY, a INT);')
        box.execute('CREATE TABLE t3(i INT PRIMARY KEY, a INT);')
        box.schema.space.create('a')
        local ret = box.execute('SHOW CREATE TABLE;')
        local res1 = {'CREATE TABLE t1(\ni INTEGER NOT NULL,\na INTEGER,\n'..
                      'CONSTRAINT pk_unnamed_t1_1 PRIMARY KEY(i))\n'..
                      "WITH ENGINE = 'memtx';"}
        local res2 = {'CREATE TABLE t2(\ni INTEGER NOT NULL,\na INTEGER,\n'..
                      'CONSTRAINT pk_unnamed_t2_1 PRIMARY KEY(i))\n'..
                      "WITH ENGINE = 'memtx';"}
        local res3 = {'CREATE TABLE t3(\ni INTEGER NOT NULL,\na INTEGER,\n'..
                      'CONSTRAINT pk_unnamed_t3_1 PRIMARY KEY(i))\n'..
                      "WITH ENGINE = 'memtx';"}
        local res4 = {"CREATE TABLE a\nWITH ENGINE = 'memtx';"}
        local err4 = {"Problem with space 'a': format is missing.",
                      "Problem with space 'a': primary key is not defined."}
        t.assert_equals(#ret.rows, 4)
        t.assert_equals(ret.rows[1][1], res1)
        t.assert_equals(ret.rows[1][2])
        t.assert_equals(ret.rows[2][1], res2)
        t.assert_equals(ret.rows[2][2])
        t.assert_equals(ret.rows[3][1], res3)
        t.assert_equals(ret.rows[3][2])
        t.assert_equals(ret.rows[4][1], res4)
        t.assert_equals(ret.rows[4][2], err4)

        box.space.a:drop()
        box.execute('DROP TABLE t1;')
        box.execute('DROP TABLE t2;')
        box.execute('DROP TABLE t3;')
    end)
end

g.test_space_from_lua = function()
    g.server:exec(function()
        -- Working example.
        local s = box.schema.space.create('a', {format = {{'i', 'integer'}}})
        s:create_index('i', {parts = {{'i', 'integer'}}})
        local res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
                     'CONSTRAINT i PRIMARY KEY(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"a"', res)
        s:drop()

        -- No columns defined.
        s = box.schema.space.create('a');
        s:create_index('i', {parts = {{1, 'integer'}}})
        res = {"CREATE TABLE a\nWITH ENGINE = 'memtx';"}
        local err = {"Problem with space 'a': format is missing.",
                     "Problem with primary key 'i': field 1 is unnamed."}
        _G.check('"a"', res, err)
        s:drop()

        -- No indexes defined.
        s = box.schema.space.create('a', {format = {{'i', 'integer'}}})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL)\n'..
               "WITH ENGINE = 'memtx';"}
        err = {"Problem with space 'a': primary key is not defined."}
        _G.check('"a"', res, err)
        s:drop()

        -- Unsupported type of index.
        s = box.schema.space.create('a', {format = {{'i', 'integer'}}})
        s:create_index('i', {type = 'hash', parts = {{'i', 'integer'}}})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL)\n'..
               "WITH ENGINE = 'memtx';"}
        err = {"Problem with space 'a': primary key has unsupported index "..
               "type."}
        _G.check('"a"', res, err)
        s:drop()

        -- Parts of PK contains unnnamed columns.
        s = box.schema.space.create('a', {format = {{'i', 'integer'}}})
        s:create_index('i', {parts = {{2, 'integer'}}})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL)\n'..
               "WITH ENGINE = 'memtx';"}
        err = {"Problem with primary key 'i': field 2 is unnamed."}
        _G.check('"a"', res, err)
        s:drop()

        -- Type of the part in PK different from type of the field.
        s = box.schema.space.create('a', {format = {{'i', 'integer'}}})
        s:create_index('i', {parts = {{'i', 'unsigned'}}})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL)\n'..
               "WITH ENGINE = 'memtx';"}
        err = {"Problem with primary key 'i': field 'i' and related part are "..
               "of different types."}
        _G.check('"a"', res, err)
        s:drop()

        -- Collation of the part in PK different from collation of the field.
        s = box.schema.space.create('a', {format = {{'i', 'string',
                                          collation = "unicode_ci"}}})
        s:create_index('i', {parts = {{'i', 'string', collation = "binary"}}})
        res = {'CREATE TABLE a(\ni STRING COLLATE unicode_ci '..
               "NOT NULL)\nWITH ENGINE = 'memtx';"}
        err = {"Problem with primary key 'i': field 'i' and related part "..
               "have different collations."}
        _G.check('"a"', res, err)
        s:drop()

        --
        -- Spaces with an engine other than "memtx" and "vinyl" cannot be
        -- created with CREATE TABLE.
        --
        res = {'CREATE TABLE "_vspace"(\n'..
               'id UNSIGNED NOT NULL,\n'..
               'owner UNSIGNED NOT NULL,\n'..
               'name STRING NOT NULL,\n'..
               'engine STRING NOT NULL,\n'..
               'field_count UNSIGNED NOT NULL,\n'..
               'flags MAP NOT NULL,\n'..
               'format ARRAY NOT NULL,\n'..
               'CONSTRAINT primary PRIMARY KEY(id));',
               'CREATE INDEX owner ON "_vspace"(owner);',
               'CREATE UNIQUE INDEX name ON "_vspace"(name);'}
        err = {"Problem with space '_vspace': wrong space engine."}
        _G.check('"_vspace"', res, err)

        -- Make sure the table, field, and PK names are properly escaped.
        s = box.schema.space.create('"A"', {format = {{'"i', 'integer'}}})
        s:create_index('123', {parts = {{'"i', 'integer'}}})
        res = {'CREATE TABLE """A"""(\n"""i" INTEGER NOT NULL,\n'..
               'CONSTRAINT "123" PRIMARY KEY("""i"))\nWITH ENGINE = \'memtx\';'}
        _G.check('"""A"""', res)
        s:drop()
    end)
end

g.test_field_foreign_key_from_lua = function()
    g.server:exec(function()
        local format = {{'i', 'integer'}}
        box.schema.space.create('a', {format = format})

        -- Working example.
        format[1].foreign_key = {a = {space = 'a', field = 'i'}}
        box.schema.space.create('b', {format = format})
        box.space.b:create_index('i', {parts = {{'i', 'integer'}}})
        local res = {'CREATE TABLE b(\ni INTEGER NOT NULL '..
                     'CONSTRAINT a REFERENCES a(i),\n'..
                     'CONSTRAINT i PRIMARY KEY(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"b"', res)

        -- Wrong foreign field defined by id in foreign_key.
        format[1].foreign_key.a = {space = 'a', field = 5}
        box.space.b:format(format)
        local err = {"Problem with foreign key 'a': foreign field is unnamed."}
        res = {'CREATE TABLE b(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"b"', res, err)

        -- Make sure field foreign key constraint name is properly escaped.
        format[1].foreign_key = {['"'] = {space = 'a', field = 'i'}}
        box.space.b:format(format)
        res = {'CREATE TABLE b(\ni INTEGER NOT NULL '..
               'CONSTRAINT """" REFERENCES a(i),\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"b"', res)

        box.space.b:drop()
        box.space.a:drop()
    end)
end

g.test_tuple_foreign_key_from_lua = function()
    g.server:exec(function()
        local opts = {format = {{'i', 'integer'}}}
        box.schema.space.create('a', opts)

        -- Working example.
        opts.foreign_key = {a = {space = 'a', field = {i = 'i'}}}
        box.schema.space.create('b', opts)
        box.space.b:create_index('i', {parts = {{'i', 'integer'}}})
        local res = {'CREATE TABLE b(\ni INTEGER NOT NULL,\n'..
                     'CONSTRAINT i PRIMARY KEY(i),\n'..
                     'CONSTRAINT a FOREIGN KEY(i) REFERENCES a(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"b"', res)

        -- Wrong foreign field defined by id in foreign_key.
        opts.foreign_key.a = {space = 'a', field = {[5] = 'i'}}
        box.space.b:alter(opts)
        res = {'CREATE TABLE b(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        local err = {"Problem with foreign key 'a': local field is unnamed."}
        _G.check('"b"', res, err)

        -- Wrong foreign field defined by id in foreign_key.
        opts.foreign_key.a = {space = 'a', field = {i = 5}}
        box.space.b:alter(opts)
        err = {"Problem with foreign key 'a': foreign field is unnamed."}
        res = {'CREATE TABLE b(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"b"', res, err)

        -- Make sure tuple foreign key constraint name is properly escaped.
        opts.foreign_key = {['a"b"c'] = {space = 'a', field = {i = 'i'}}}
        box.space.b:alter(opts)
        res = {'CREATE TABLE b(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i),\n'..
               'CONSTRAINT "a""b""c" FOREIGN KEY(i) REFERENCES a(i))\n'..
               "WITH ENGINE = 'memtx';"}
        _G.check('"b"', res)

        box.space.b:drop()
        box.space.a:drop()
    end)
end

g.test_field_check_from_lua = function()
    g.server:exec(function()
        box.schema.func.create('f', {body = '"i" > 10', language = 'SQL_EXPR',
                                     is_deterministic = true})
        box.schema.func.create('f1', {body = 'function(a) return a > 10 end',
                                     is_deterministic = true})
        box.schema.func.create('f2', {body = '"b" > 10', language = 'SQL_EXPR',
                                     is_deterministic = true})

        -- Working example.
        local format = {{'i', 'integer', constraint = {a = 'f'}}}
        box.schema.space.create('a', {format = format})
        box.space.a:create_index('i', {parts = {{'i', 'integer'}}})
        local res = {'CREATE TABLE a(\ni INTEGER NOT NULL '..
                     'CONSTRAINT a CHECK("i" > 10),\n'..
                     'CONSTRAINT i PRIMARY KEY(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"a"', res)

        -- Wrong function type.
        format[1].constraint.a = 'f1'
        box.space.a:format(format)
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        local err = {"Problem with check constraint 'a': wrong constraint "..
                     "expression."}
        _G.check('"a"', res, err)

        -- Wrong field name in the function.
        format[1].constraint.a = 'f2'
        box.space.a:format(format)
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        err = {"Problem with check constraint 'a': wrong field name in "..
               "constraint expression."}
        _G.check('"a"', res, err)

        -- Make sure field check constraint name is properly escaped.
        format[1].constraint = {['""'] = 'f'}
        box.space.a:format(format)
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL '..
               'CONSTRAINT """""" CHECK("i" > 10),\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"a"', res)

        box.space.a:drop()
        box.func.f:drop()
        box.func.f1:drop()
        box.func.f2:drop()
    end)
end

g.test_tuple_check_from_lua = function()
    g.server:exec(function()
        box.schema.func.create('f', {body = '"i" > 10', language = 'SQL_EXPR',
                                     is_deterministic = true})
        box.schema.func.create('f1', {body = 'function(a) return a > 10 end',
                                     is_deterministic = true})
        box.schema.func.create('f2', {body = '1 > 0', language = 'SQL_EXPR',
                                     is_deterministic = true})
        box.schema.func.create('f3', {body = 'k > l', language = 'SQL_EXPR',
                                     is_deterministic = true})

        -- Working example.
        local opts = {format = {{'i', 'integer'}}, constraint = {a = 'f'}}
        box.schema.space.create('a', opts)
        box.space.a:create_index('i', {parts = {{'i', 'integer'}}})
        local res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
                     'CONSTRAINT i PRIMARY KEY(i),\n'..
                     'CONSTRAINT a CHECK("i" > 10))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"a"', res)

        -- Wrong function type.
        opts.constraint.a = 'f1'
        box.space.a:alter(opts)
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        local err = {"Problem with check constraint 'a': wrong constraint "..
                     "expression."}
        _G.check('"a"', res, err)

        -- Make sure tuple check constraint name is properly escaped.
        opts.constraint = {['"a"'] = 'f'}
        box.space.a:alter(opts)
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i),\n'..
               'CONSTRAINT """a""" CHECK("i" > 10))\nWITH ENGINE = \'memtx\';'}
        _G.check('"a"', res)

        -- Wrong function arguments.
        opts.constraint = {a = 'f3'}
        box.space.a:alter(opts)
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        err = {"Problem with check constraint 'a': wrong field name in "..
               "constraint expression."}
        _G.check('"a"', res, err)
        box.space.a:drop()

        -- The columns are not defined, but a constraint is set.
        box.schema.space.create('a', {constraint = {a = 'f2'}})
        res = {'CREATE TABLE a(\nCONSTRAINT a CHECK(1 > 0))\n'..
               'WITH ENGINE = \'memtx\';'}
        err = {"Problem with space 'a': format is missing.",
               "Problem with space 'a': primary key is not defined."}
        _G.check('"a"', res, err)
        box.space.a:drop()

        box.func.f:drop()
        box.func.f1:drop()
        box.func.f2:drop()
        box.func.f3:drop()
    end)
end

g.test_wrong_collation = function()
    g.server:exec(function()
        local map = setmetatable({}, { __serialize = 'map' })
        local col_def = {'col1', 1, 'BINARY', '', map}
        local col = box.space._collation:auto_increment(col_def)
        t.assert(col ~= nil)

        -- Working example.
        local format = {{'i', 'string', collation = 'col1'}}
        box.schema.space.create('a', {format = format})
        local parts = {{'i', 'string', collation = 'col1'}}
        box.space.a:create_index('i', {parts = parts})
        local res = {'CREATE TABLE a(\ni STRING COLLATE col1'..
                     ' NOT NULL,\nCONSTRAINT i PRIMARY KEY(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"a"', res)

        box.space.a:drop()
        box.space._collation:delete(col.id)

        -- Make sure collation name is properly escaped.
        col_def = {'"c"ol"2', 1, 'BINARY', '', map}
        col = box.space._collation:auto_increment(col_def)
        t.assert(col ~= nil)
        format = {{'i', 'string', collation = '"c"ol"2'}}
        box.schema.space.create('a', {format = format})
        parts = {{'i', 'string', collation = '"c"ol"2'}}
        box.space.a:create_index('i', {parts = parts})
        res = {'CREATE TABLE a(\ni STRING COLLATE """c""ol""2" '..
               'NOT NULL,\nCONSTRAINT i PRIMARY KEY(i))\n'..
               "WITH ENGINE = 'memtx';"}
        _G.check('"a"', res)

        box.space.a:drop()
        box.space._collation:delete(col.id)
    end)
end

g.test_index_from_lua = function()
    g.server:exec(function()
        local format = {{'i', 'integer'}, {'s', 'string', collation = 'binary'}}
        local s = box.schema.space.create('a', {format = format})
        s:create_index('i', {parts = {{'i', 'integer'}}})
        local res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
                     's STRING COLLATE binary NOT NULL,\n'..
                     'CONSTRAINT i PRIMARY KEY(i))\n'..
                     "WITH ENGINE = 'memtx';"}
        _G.check('"a"', res)

        -- Working example.
        s:create_index('i1', {parts = {{'i', 'integer'}}})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';',
               'CREATE UNIQUE INDEX i1 ON a(i);'}
        _G.check('"a"', res)
        s.index.i1:drop()

        -- Unsupported type of the index.
        s:create_index('i1', {parts = {{'i', 'integer'}}, type = 'HASH'})
        local err = {"Problem with index 'i1': unsupported index type."}
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"a"', res, err)
        s.index.i1:drop()

        -- Non-unique index.
        s:create_index('i1', {parts = {{'i', 'integer'}}, unique = false})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';',
               'CREATE INDEX i1 ON a(i);'}
        _G.check('"a"', res)
        s.index.i1:drop()

        -- Parts contains an unnamed field.
        s:create_index('i1', {parts = {{5, 'integer'}}})
        err = {"Problem with index 'i1': field 5 is unnamed."}
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"a"', res, err)
        s.index.i1:drop()

        -- Type of the part in index different from type of the field.
        s:create_index('i1', {parts = {{'i', 'unsigned'}}})
        err = {"Problem with index 'i1': field 'i' and related part are of "..
               "different types."}
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"a"', res, err)
        s.index.i1:drop()

        -- Collation of the part in index different from collation of the field.
        s:create_index('i1', {parts = {{'s', 'string', collation = "unicode"}}})
        err = {"Problem with index 'i1': field 's' and related part have "..
               "different collations."}
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';'}
        _G.check('"a"', res, err)
        s.index.i1:drop()

        -- Make sure index name is properly escaped.
        s:create_index('i7"', {parts = {{'i', 'integer'}}})
        res = {'CREATE TABLE a(\ni INTEGER NOT NULL,\n'..
               's STRING COLLATE binary NOT NULL,\n'..
               'CONSTRAINT i PRIMARY KEY(i))\nWITH ENGINE = \'memtx\';',
               'CREATE UNIQUE INDEX "i7""" ON a(i);'}
        _G.check('"a"', res)

        s:drop()
    end)
end
