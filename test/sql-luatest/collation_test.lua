local server = require('luatest.server')
local t = require('luatest')

local g = t.group("collation", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_collation = function(cg)
    cg.server:exec(function()
        -- Explicitly set BINARY collation is predefined and has ID.
        box.execute([[CREATE TABLE t (id INT PRIMARY KEY,
                      a TEXT, b TEXT COLLATE "binary");]])
        t.assert(box.space.t ~= nil)
        local res = box.space.t:format()[2]['collation']
        t.assert_equals(res, nil)
        res = box.space.t:format()[3]['collation']
        t.assert_equals(res, 3)
        box.execute("DROP TABLE t;")

        -- BINARY collation works in the same way as there is no collation
        -- at all.
        local f = {{'id', 'unsigned'}, {'a', 'string', collation = 'binary'}}
        local test = box.schema.create_space('test', {format = f})
        res = test:format()[2]['collation']
        t.assert_equals(res, 3)
        test:create_index('primary', {parts = {1}})
        local i = test:create_index('secondary', {parts = {2, 'str',
                                                  collation='binary'}})
        res = test:insert({1, 'AsD'})
        t.assert_equals(res, {1, 'AsD'})
        res = test:insert({2, 'ASD'})
        t.assert_equals(res, {2, 'ASD'})
        res = test:insert({3, 'asd'})
        t.assert_equals(res, {3, 'asd'})
        res = i:select('asd')
        t.assert_equals(res, {{3, 'asd'}})
        res = i:select('ASD')
        t.assert_equals(res, {{2, 'ASD'}})
        res = i:select('AsD')
        t.assert_equals(res, {{1, 'AsD'}})
        test:drop()

        -- Collation with id == 0 is "none". It used to unify interaction
        -- with collation interface. It also can't be dropped.
        res = box.space._collation:select{0}
        t.assert_equals(res, {{0, 'none', 1, 'BINARY', '', {}}})
        local exp_err = "Can't drop collation 'none': system collation"
        t.assert_error_msg_equals(exp_err, box.space._collation.delete,
                                  box.space._collation, {0})
    end)
end

g.test_ghs_80 = function(cg)
    cg.server:exec(function()
        local sql = [[select 'a' collate a union select 'b' collate "binary";]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, "Collation 'a' does not exist")
    end)
end

g.test_gh_9229 = function(cg)
    cg.server:exec(function()
        local sql = [[CREATE TABLE t(i INT, a INT, PRIMARY KEY(i, a),
                      b STRING UNIQUE COLLATE "unicode_ci");]]
        box.execute(sql)
        t.assert(box.space.t ~= nil)
        t.assert_equals(box.space.t.index[1].parts[1].collation, "unicode_ci")
        box.execute([[DROP TABLE t;]])
    end)
end

--
-- gh-3010: COLLATE after LIMIT should throw an error
--
g.test_gh_3010 = function(cg)
    cg.server:exec(function()
        -- All of these tests should throw error "near "COLLATE": syntax error"
        local _, err = box.execute("SELECT 1 LIMIT 1 COLLATE BINARY;")
        t.assert_equals(err.message, "Syntax error at line 1 near 'COLLATE'")
        _, err = box.execute("SELECT 1 LIMIT 1 COLLATE BINARY OFFSET 1;")
        t.assert_equals(err.message, "Syntax error at line 1 near 'COLLATE'")
        _, err = box.execute("SELECT 1 LIMIT 1 OFFSET 1 COLLATE BINARY;")
        t.assert_equals(err.message, "Syntax error at line 1 near 'COLLATE'")
        _, err = box.execute("SELECT 1 LIMIT 1, 1 COLLATE BINARY;")
        t.assert_equals(err.message, "Syntax error at line 1 near 'COLLATE'")
        _, err = box.execute("SELECT 1 LIMIT 1 COLLATE BINARY, 1;")
        t.assert_equals(err.message, "Syntax error at line 1 near 'COLLATE'")
    end)
end

--
-- gh-3052: UPPER/LOWER support only default locale
--
g.test_gh_3052 = function(cg)
    cg.server:exec(function()
        -- For tr-TR result depends on collation
        box.execute([[CREATE TABLE tu (descriptor VARCHAR(50)
                      PRIMARY KEY, letter VARCHAR(50))]])
        t.assert(box.space.tu ~= nil)
        box.internal.collation.create('TURKISH', 'ICU', 'tr-TR',
                                      {strength='primary'})
        box.execute([[INSERT INTO tu VALUES
                      ('Latin Capital Letter I U+0049','I');]])
        box.execute([[INSERT INTO tu VALUES
                      ('Latin Small Letter I U+0069','i');]])
        box.execute([[INSERT INTO tu VALUES
                      ('Latin Capital Letter I With Dot Above U+0130','İ');]])
        box.execute([[INSERT INTO tu VALUES
                      ('Latin Small Letter Dotless I U+0131','ı');]])
        -- Without collation
        local res = box.execute([[SELECT descriptor, UPPER(letter) AS upper,
                                  LOWER(letter) AS lower FROM tu;]])
        local exp = {
            metadata = {
                {name = "descriptor", type = "string"},
                {name = "upper", type = "string"},
                {name = "lower", type = "string"},
            },
            rows = {
                {"Latin Capital Letter I U+0049", "I", "i"},
                {"Latin Capital Letter I With Dot Above U+0130", "İ", "i̇"},
                {"Latin Small Letter Dotless I U+0131", "I", "ı"},
                {"Latin Small Letter I U+0069", "I", "i"},
            },
        }
        t.assert_equals(res, exp)
        -- With collation
        res = box.execute([[SELECT descriptor,
                            UPPER(letter COLLATE "TURKISH") AS upper,
                            LOWER(letter COLLATE "TURKISH") AS lower FROM tu;]])
        exp = {
            metadata = {
                {name = "descriptor", type = "string"},
                {name = "upper", type = "string"},
                {name = "lower", type = "string"},
            },
            rows = {
                {'Latin Capital Letter I U+0049', 'I', 'ı'},
                {'Latin Capital Letter I With Dot Above U+0130', 'İ', 'i'},
                {'Latin Small Letter Dotless I U+0131', 'I', 'ı'},
                {'Latin Small Letter I U+0069', 'İ', 'i'},
            },
        }
        t.assert_equals(res, exp)
        box.internal.collation.drop('TURKISH')

        -- For de-DE result is actually the same
        box.internal.collation.create('GERMAN', 'ICU', 'de-DE',
                                      {strength='primary'})
        box.execute([[INSERT INTO tu
                      VALUES('German Small Letter Sharp S U+00DF','ß');]])
        -- Without collation
        res = box.execute([[SELECT descriptor, UPPER(letter),
                            letter FROM tu where UPPER(letter) = 'SS';]])
        exp = {
            metadata = {
                {name = "descriptor", type = "string"},
                {name = "COLUMN_1", type = "string"},
                {name = "letter", type = "string"},
            },
            rows ={
                {'German Small Letter Sharp S U+00DF', 'SS', 'ß'},
            },
        }
        t.assert_equals(res, exp)
        -- With collation
        res = box.execute([[SELECT descriptor, UPPER(letter COLLATE "GERMAN"),
                            letter FROM tu
                            where UPPER(letter COLLATE "GERMAN") = 'SS';]])
        t.assert_equals(res, exp)
        box.internal.collation.drop('GERMAN')
        box.execute(([[DROP TABLE tu]]))

        box.schema.user.create('test_user', {password = 'test'})
        box.session.su('admin', box.schema.user.grant,
                       'test_user', 'read,write,execute', 'universe')

        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user = 'test_user', password = 'test'})

        local sql = 'SELECT 1 limit ? COLLATE not_exist'
        local exp_err = "Syntax error at line 1 near 'COLLATE'"
        t.assert_error_msg_equals(exp_err, cn.execute, cn, sql, {1})

        cn:close()
        box.schema.user.revoke('test_user', 'read,write,execute', 'universe')
        box.schema.user.drop('test_user')
    end)
end

--
-- gh-3185: collations of LHS and RHS must be compatible.
--
g.test_gh_3185 = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (id INT PRIMARY KEY, a TEXT,
                      b TEXT COLLATE "binary", c TEXT COLLATE "unicode_ci");]])
        t.assert(box.space.t ~= nil)
        local res = box.execute([[SELECT * FROM t WHERE a = b;]])
        local exp = {
            metadata = {
                {name = "id", type = "integer"},
                {name = "a", type = "string"},
                {name = "b", type = "string"},
                {name = "c", type = "string"},
            },
            rows = {},
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT * FROM t WHERE a COLLATE "binary" = b;]])
        t.assert_equals(res, exp)
        local _, err = box.execute([[SELECT * FROM t WHERE b = c;]])
        local exp_err = "Illegal mix of collations"
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT * FROM t WHERE b COLLATE "binary" = c;]])
        t.assert_equals(res, exp)
        res = box.execute([[SELECT * FROM t WHERE a = c;]])
        t.assert_equals(res, exp)
        _, err = box.execute([[SELECT * FROM t WHERE a
                               COLLATE "binary" = c COLLATE "unicode";]])
        t.assert_equals(err.message, exp_err)
        -- Make sure that using function featuring variable arguments
        -- length  and resulting collation which depends on arguments
        -- is processed correctly.
        _, err = box.execute([[SELECT * FROM t WHERE a
                               COLLATE "binary" = SUBSTR();]])
        exp_err = "Wrong number of arguments is passed to SUBSTR(): "..
                  "expected from 2 to 3, got 0"
        t.assert_equals(err.message, exp_err)

        -- Compound queries perform implicit comparisons between values.
        -- Hence, rules for collations compatibilities are the same.
        _, err = box.execute([[SELECT 'abc' COLLATE "binary" UNION
                               SELECT 'ABC' COLLATE "unicode_ci"]])
        exp_err = "Illegal mix of collations"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[SELECT 'abc' COLLATE "unicode_ci" UNION
                               SELECT 'ABC' COLLATE "binary"]])
        t.assert_equals(err.message, exp_err)
        _, err = box.execute([[SELECT c FROM t UNION SELECT b FROM t;]])
        t.assert_equals(err.message, exp_err)
        res = box.execute([[SELECT b FROM t UNION SELECT a FROM t;]])
        exp = {
            metadata = {
                {name = "b", type = "string"},
            },
            rows = {},
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT a FROM t UNION SELECT c FROM t;]])
        exp = {
            metadata = {
                {name = "a", type = "string"},
            },
            rows = {},
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT c COLLATE "binary" FROM t UNION
                            SELECT a FROM t;]])
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {},
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT b COLLATE "unicode"
                            FROM t UNION SELECT a FROM t;]])
                      t.assert_equals(res, exp)

        box.execute("DROP TABLE t;")
    end)
end

--
-- gh-3857 "PRAGMA collation_list" invokes segmentation fault.
-- gh-4713 "PRAGMA collation_list" is not accessible to all users
--
g.test_ghs_3857_4713 = function (cg)
    cg.server:exec(function ()
        box.schema.user.create('tmp')
        box.session.su('tmp')
        local _, err = box.execute('pragma collation_list')
        t.assert(err == nil)
        box.session.su('admin')
        box.schema.user.drop('tmp')
    end)
end

g.test_gh_3644 = function (cg)
    cg.server:exec(function ()
        -- Check that foreign key update doesn't fail with "unicode_ci".
        box.execute([[CREATE TABLE t0 (s1 VARCHAR(5)
                      COLLATE "unicode_ci" UNIQUE,
                      id INT PRIMARY KEY AUTOINCREMENT);]])
        t.assert(box.space.t0 ~= nil)
        box.execute([[CREATE TABLE t1 (s1 INT PRIMARY KEY, s0 VARCHAR(5)
                      COLLATE "unicode_ci" REFERENCES t0(s1));]])
        t.assert(box.space.t1 ~= nil)
        local res = box.execute("INSERT INTO t0(s1) VALUES ('a');")
        local exp = {
            autoincrement_ids = {1},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        box.execute("INSERT INTO t1 VALUES (1,'a');")
        -- Shouldn't fail.
        box.execute("UPDATE t0 SET s1 = 'A';")
        res = box.execute("SELECT s1 FROM t0;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {{'A'}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT * FROM t1;")
        exp = {
            metadata = {
                {name = 's1', type = 'integer'},
                {name = 's0', type = 'string'},
            },
            rows = {{1, 'a'}}
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t1;")
        box.execute("DROP TABLE t0;")
        -- Check that foreign key update fails with default collation.
        box.execute([[CREATE TABLE t0 (s1 VARCHAR(5) UNIQUE,
                      id INT PRIMARY KEY AUTOINCREMENT);]])
        t.assert(box.space.t0 ~= nil)
        box.execute([[CREATE TABLE t1 (s1 INT PRIMARY KEY,
                      s0 VARCHAR(5) REFERENCES t0(s1));]])
        t.assert(box.space.t1 ~= nil)
        res = box.execute("INSERT INTO t0(s1) VALUES ('a');")
        exp = {
            autoincrement_ids = {1},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        box.execute("INSERT INTO t1 VALUES (1,'a');")
        -- Should fail.
        local _, err = box.execute("UPDATE t0 SET s1 = 'A';")
        local exp_err = "Foreign key 'fk_unnamed_t1_s0_1' integrity check "..
                        "failed: index was not found"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t1;")
        exp = {
            metadata = {
                {name = "s1", type = "integer"},
                {name = "s0", type = "string"},
            },
            rows = {{1, 'a'}},
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT s1 FROM t0;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {{'a'}},
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t1;")
        box.execute("DROP TABLE t0;")
    end)
end

g.test_gh_3937 = function (cg)
    cg.server:exec(function ()
        box.execute([[CREATE TABLE t4a(a TEXT COLLATE "unicode",
                      b TEXT COLLATE "unicode_ci", c INT PRIMARY KEY);]])
        t.assert(box.space.t4a ~= nil)
        box.execute("INSERT INTO t4a VALUES('ABC','abc',1);")
        box.execute("INSERT INTO t4a VALUES('ghi','ghi',3);")
        -- Only LHS of concatenation has implicitly set collation.
        -- Hence, no collation is used.
        --
        local res = box.execute("SELECT c FROM t4a WHERE (a||'') = b;")
        local exp = {
            metadata = {
                {name = "c", type = "integer"},
            },
            rows = {
                {1},
                {3},
            },
        }
        t.assert_equals(res, exp)
        -- BINARY collation is forced for comparison operator as
        -- a derivation from concatenation.
        --
        res = box.execute([[SELECT c FROM t4a WHERE
                            (a COLLATE "binary"||'') = b;]])
        exp = {
            metadata = {
                {name = "c", type = "integer"},
            },
            rows = {
                {3},
            },
        }
        t.assert_equals(res, exp)
        -- Both operands of concatenation have explicit but different
        -- collations.
        --
        local _, err = box.execute([[SELECT c FROM t4a WHERE
                                     (a COLLATE "binary"||''
                                     COLLATE "unicode_ci") = b;]])
        t.assert_equals(err.message, "Illegal mix of collations")

        _, err = box.execute([[SELECT c FROM t4a WHERE
                               (a COLLATE "binary"||'') = b
                               COLLATE "unicode";]])
        t.assert_equals(err.message, "Illegal mix of collations")
        -- No collation is used since LHS and RHS of concatenation
        -- operator have different implicit collations.
        --
        res = box.execute("SELECT c FROM t4a WHERE (a||'')=(b||'');")
        t.assert_equals(res, exp)
        res = box.execute("SELECT c FROM t4a WHERE (a||b)=(b||a);")
        t.assert_equals(res, exp)

        box.execute([[CREATE TABLE t4b(a TEXT COLLATE "unicode_ci",
                      b TEXT COLLATE "unicode_ci", c INT PRIMARY KEY);]])
        t.assert(box.space.t4b ~= nil)
        box.execute("INSERT INTO t4b VALUES('BCD','bcd',1);")
        box.execute("INSERT INTO t4b VALUES('ghi','ghi',3);")
        -- Operands have the same implicit collation, so it is derived.
        --
        res = box.execute("SELECT c FROM t4a WHERE (a||b)=(b||a);")
        t.assert_equals(res, exp)
        -- Couple of other possible combinations.
        --
        res = box.execute([[SELECT c FROM t4a WHERE
                            (a||b COLLATE "binary")=(b||a);]])
        t.assert_equals(res, exp)
        _, err = box.execute([[SELECT c FROM t4a WHERE
                            (a||b COLLATE "binary")=
                            (b COLLATE "unicode_ci"||a);]])
        t.assert_equals(err.message, "Illegal mix of collations")

        box.execute("INSERT INTO t4b VALUES('abc', 'xxx', 2);")
        box.execute("INSERT INTO t4b VALUES('gHz', 'xxx', 4);")
        -- Results are sorted with case-insensitive order.
        -- Otherwise capital latters come first.
        --
        res = box.execute([[SELECT a FROM t4b
                            ORDER BY a COLLATE "unicode_ci" || '']])
        exp = {
            metadata = {
                {name = "a", type = "string"},
            },
            rows = {
                {'abc'},
                {'BCD'},
                {'ghi'},
                {'gHz'},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT a FROM t4b ORDER BY a || b")
        t.assert_equals(res, exp)

        box.space.t4a:drop()
        box.space.t4b:drop()
    end)
end

--
-- gh-3537 Duplicate key error for an index that is not unique
--
g.test_gh_3537 = function(cg)
    cg.server:exec(function ()
        -- pk - default, sc - unicode_ci
        box.execute('CREATE TABLE t3 (s1 VARCHAR(5) PRIMARY KEY);')
        t.assert(box.space.t3 ~= nil)
        box.execute('CREATE INDEX i3 ON t3 (s1 collate "unicode_ci");')
        box.execute("INSERT INTO t3 VALUES ('a');")
        box.execute("INSERT INTO t3 VALUES ('A');")
        local res = box.execute("SELECT * FROM t3;")
        local exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {
                {'A'},
                {'a'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3;")

        -- pk - binary, sc - unicode
        box.execute([[CREATE TABLE t3b (s1 VARCHAR(5)
                      collate "binary" PRIMARY KEY);]])
        t.assert(box.space.t3b ~= nil)
        box.execute('CREATE INDEX i3b ON t3b (s1 collate "unicode");')
        box.execute("INSERT INTO t3b VALUES ('a');")
        box.execute("INSERT INTO t3b VALUES ('A');")
        res = box.execute("SELECT * FROM t3b;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3b;")

        -- pk - binary, sc - unicode (make dup)
        box.execute([[CREATE TABLE t3b (s1 VARCHAR(5)
                      collate "binary" PRIMARY KEY);]])
        t.assert(box.space.t3b ~= nil)
        box.execute('CREATE INDEX i3b ON t3b (s1 collate "unicode");')
        box.execute("INSERT INTO t3b VALUES ('a');")
        box.execute("INSERT INTO t3b VALUES ('A');")
        local _, err = box.execute("INSERT INTO t3b VALUES ('a');")
        local exp_err = "Duplicate key exists in unique index "..
                        "\"pk_unnamed_t3b_1\" in space \"t3b\" with old "..
                        "tuple - [\"a\"] and new tuple - [\"a\"]"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3b;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3b;")

        -- pk - unicode, sc - binary
        box.execute([[CREATE TABLE t3c (s1 VARCHAR(5)
                      collate "unicode" PRIMARY KEY);]])
        t.assert(box.space.t3c ~= nil)
        box.execute('CREATE INDEX i3c ON t3c (s1 collate "binary");')
        box.execute("INSERT INTO t3c VALUES ('a');")
        box.execute("INSERT INTO t3c VALUES ('A');")
        res = box.execute("SELECT * FROM t3c;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {
                {'a'},
                {'A'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3c;")

        -- pk - unicode, sc - binary (make dup)
        box.execute([[CREATE TABLE t3c (s1 VARCHAR(5)
                      collate "unicode" PRIMARY KEY);]])
        t.assert(box.space.t3c ~= nil)
        box.execute('CREATE INDEX i3c ON t3c (s1 collate "binary");')
        box.execute("INSERT INTO t3c VALUES ('a');")
        box.execute("INSERT INTO t3c VALUES ('A');")
        _, err = box.execute("INSERT INTO t3c VALUES ('a');")
        exp_err = "Duplicate key exists in unique index "..
                  "\"pk_unnamed_t3c_1\" in space \"t3c\" with old "..
                  "tuple - [\"a\"] and new tuple - [\"a\"]"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3c;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3c;")

        -- pk - binary, sc - unicode_ci
        box.execute([[CREATE TABLE t3d (s1 VARCHAR(5)
                      collate "binary" PRIMARY KEY);]])
        t.assert(box.space.t3d ~= nil)
        box.execute('CREATE INDEX i3d ON t3d (s1 collate "unicode_ci");')
        box.execute("INSERT INTO t3d VALUES ('a');")
        box.execute("INSERT INTO t3d VALUES ('A');")
        res = box.execute("SELECT * FROM t3d;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {
                {'A'},
                {'a'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3d;")

        -- pk - binary, sc - unicode_ci (make dup)
        box.execute([[CREATE TABLE t3d (s1 VARCHAR(5)
                      collate "binary" PRIMARY KEY);]])
        t.assert(box.space.t3d ~= nil)
        box.execute('CREATE INDEX i3d ON t3d (s1 collate "unicode_ci");')
        box.execute("INSERT INTO t3d VALUES ('a');")
        box.execute("INSERT INTO t3d VALUES ('A');")
        _, err = box.execute("INSERT INTO t3d VALUES ('a');")
        exp_err = "Duplicate key exists in unique index "..
                  "\"pk_unnamed_t3d_1\" in space \"t3d\" with old "..
                  "tuple - [\"a\"] and new tuple - [\"a\"]"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3d;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3d;")

        -- pk - unicode_ci, sc - binary (should fail)
        box.execute([[CREATE TABLE t3e (s1 VARCHAR(5)
                      collate "unicode_ci" PRIMARY KEY);]])
        t.assert(box.space.t3e ~= nil)
        box.execute('CREATE INDEX i3e ON t3e (s1 collate "binary");')
        box.execute("INSERT INTO t3e VALUES ('a');")
        _, err = box.execute("INSERT INTO t3e VALUES ('A');")
        exp_err = "Duplicate key exists in unique index "..
                  "\"pk_unnamed_t3e_1\" in space \"t3e\" with old "..
                  "tuple - [\"a\"] and new tuple - [\"A\"]"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3e;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {
                {'a'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3e;")

        -- pk - unicode, sc - unicode_ci
        box.execute([[CREATE TABLE t3f (s1 VARCHAR(5)
                      collate "unicode" PRIMARY KEY);]])
        t.assert(box.space.t3f ~= nil)
        box.execute('CREATE INDEX i3f ON t3f (s1 collate "unicode_ci");')
        box.execute("INSERT INTO t3f VALUES ('a');")
        box.execute("INSERT INTO t3f VALUES ('A');")
        res = box.execute("SELECT * FROM t3f;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {
                {'a'},
                {'A'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3f;")

        -- pk - unicode, sc - unicode_ci (make dup)
        box.execute([[CREATE TABLE t3f (s1 VARCHAR(5)
                      collate "unicode" PRIMARY KEY);]])
        t.assert(box.space.t3f ~= nil)
        box.execute('CREATE INDEX i3f ON t3f (s1 collate "unicode_ci");')
        box.execute("INSERT INTO t3f VALUES ('a');")
        box.execute("INSERT INTO t3f VALUES ('A');")
        _, err = box.execute("INSERT INTO t3f VALUES ('a');")
        exp_err = "Duplicate key exists in unique index "..
                  "\"pk_unnamed_t3f_1\" in space \"t3f\" with old "..
                  "tuple - [\"a\"] and new tuple - [\"a\"]"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3f;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3f;")

        -- pk - unicode_ci, sc - unicode (should fail)
        box.execute([[CREATE TABLE t3g (s1 VARCHAR(5)
                      collate "unicode_ci" PRIMARY KEY);]])
        t.assert(box.space.t3g ~= nil)
        box.execute('CREATE INDEX i3g ON t3g (s1 collate "unicode");')
        box.execute("INSERT INTO t3g VALUES ('a');")
        _, err = box.execute("INSERT INTO t3g VALUES ('A');")
        exp_err = "Duplicate key exists in unique index "..
                  "\"pk_unnamed_t3g_1\" in space \"t3g\" with old "..
                  "tuple - [\"a\"] and new tuple - [\"A\"]"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t3g;")
        exp = {
            metadata = {
                {name = "s1", type = "string"},
            },
            rows = {
                {'a'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE t3g;")

        -- pk - default, sc - multipart
        box.execute([[CREATE TABLE qms1 (w VARCHAR(5) PRIMARY KEY,
                      n VARCHAR(5), q VARCHAR(5), s INTEGER);]])
        t.assert(box.space.qms1 ~= nil)
        box.execute('CREATE INDEX iqms1 ON qms1 (w collate "unicode_ci", n);')
        box.execute("INSERT INTO qms1 VALUES ('www', 'nnn', 'qqq', 1);")
        box.execute("INSERT INTO qms1 VALUES ('WWW', 'nnn', 'qqq', 2);")
        res = box.execute("SELECT * FROM qms1;")
        exp = {
            metadata = {
                {name = "w", type = "string"},
                {name = "n", type = "string"},
                {name = "q", type = "string"},
                {name = "s", type = "integer"},
            },
            rows = {
                {'WWW', 'nnn', 'qqq', 2},
                {'www', 'nnn', 'qqq', 1},
            },
        }
        t.assert_equals(res, exp)
        box.execute("DROP TABLE qms1;")

        box.execute([[CREATE TABLE qms2 (w VARCHAR(5) PRIMARY KEY,
                      n VARCHAR(5), q VARCHAR(5), s INTEGER);]])
        t.assert(box.space.qms2 ~= nil)
        box.execute('CREATE INDEX iqms2 ON qms2 (w collate "unicode", n);')
        box.execute("INSERT INTO qms2 VALUES ('www', 'nnn', 'qqq', 1);")
        box.execute("INSERT INTO qms2 VALUES ('WWW', 'nnn', 'qqq', 2);")
        res = box.execute("SELECT * FROM qms2;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE qms2;")

        -- pk - multipart, sc overlaps with pk
        box.execute([[CREATE TABLE qms3 (w VARCHAR(5), n VARCHAR(5),
                      q VARCHAR(5), s INTEGER,
                      CONSTRAINT pk_qms3 PRIMARY KEY(w, n, q));]])
        t.assert(box.space.qms3 ~= nil)
        box.execute('CREATE INDEX iqms3 ON qms3 (w collate "unicode_ci", s);')
        box.execute("INSERT INTO qms3 VALUES ('www', 'nnn', 'qqq', 1);")
        box.execute("INSERT INTO qms3 VALUES ('WWW', 'nnn', 'qqq', 2);")
        res = box.execute("SELECT * FROM qms3;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE qms3;")

        box.execute([[CREATE TABLE qms4 (w VARCHAR(5), n VARCHAR(5),
                      q VARCHAR(5), s INTEGER,
                      CONSTRAINT pk_qms4 PRIMARY KEY(w, n, q));]])
        t.assert(box.space.qms4 ~= nil)
        box.execute('CREATE INDEX iqms4 ON qms4 (w collate "unicode", s);')
        box.execute("INSERT INTO qms4 VALUES ('www', 'nnn', 'qqq', 1);")
        box.execute("INSERT INTO qms4 VALUES ('WWW', 'nnn', 'qqq', 2);")
        res = box.execute("SELECT * FROM qms4;")
        t.assert_equals(res, exp)
        box.execute("DROP TABLE qms4;")
    end)
end

--
-- gh-3932: make sure set built-in functions derive collation
-- from their arguments.
--
g.test_gh_3932 = function (cg)
    cg.server:exec(function ()
        box.execute([[CREATE TABLE jj (s1 INT PRIMARY KEY,
                      s2 VARCHAR(3) COLLATE "unicode_ci");]])
        t.assert(box.space.jj ~= nil)
        box.execute("INSERT INTO jj VALUES (1,'A'), (2,'a')")
        local res = box.execute("SELECT DISTINCT TRIM(s2) FROM jj;")
        local exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {'A'},
            },
        }
        t.assert_equals(res, exp)
        box.execute("INSERT INTO jj VALUES (3, 'aS'), (4, 'AS');")
        res = box.execute("SELECT DISTINCT REPLACE(s2, 'S', 's') FROM jj;")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {'A'},
                {'as'},
            },
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT DISTINCT SUBSTR(s2, 1, 1) FROM jj;")
        exp = {
            metadata = {
                {name = "COLUMN_1", type = "string"},
            },
            rows = {
                {'A'},
            },
        }
        t.assert_equals(res, exp)
        box.space.jj:drop()
    end)
end

--
-- gh-3573: Strength in the _collation space
--
g.test_gh_3573 = function (cg)
    cg.server:exec(function ()
        -- Collation without 'strength' option set now has explicit
        -- 'strength' = 'tertiary'.
        box.internal.collation.create('c', 'ICU', 'unicode')
        local res = box.space._collation.index.name:get({'c'})
        local exp = {277, 'c', 0, 'ICU', 'unicode', {strength = "tertiary"}}
        t.assert_equals(res, exp)
        box.internal.collation.drop('c')
    end)
end

--
-- gh-4007 Feature request for a new collation
--
g.test_gh_4007 = function (cg)
    cg.server:exec(function ()
        -- Default unicode collation deals with russian letters
        local s = box.schema.space.create('t1')
        t.assert(box.space.t1 ~= nil)
        s:format({{name='s1', type='string', collation = 'unicode'}})
        s:create_index('pk', {unique = true, type='tree',
                              parts={{'s1', collation = 'unicode'}}})
        local res = s:insert{'Ё'}
        t.assert_equals(res, {'Ё'})
        res = s:insert{'Е'}
        t.assert_equals(res, {'Е'})
        res = s:insert{'ё'}
        t.assert_equals(res, {'ё'})
        res = s:insert{'е'}
        t.assert_equals(res, {'е'})
        -- all 4 letters are in the table
        res = s:select{}
        local exp = {
            {'е'},
            {'Е'},
            {'ё'},
            {'Ё'},
        }
        t.assert_equals(res, exp)
        s:drop()

        -- unicode_ci collation doesn't distinguish russian letters 'Е' and 'Ё'
        s = box.schema.space.create('t1')
        t.assert(box.space.t1 ~= nil)
        s:format({{name='s1', type='string', collation = 'unicode_ci'}})
        s:create_index('pk', {unique = true, type='tree',
                             parts={{'s1', collation = 'unicode_ci'}}})
        res = s:insert{'Ё'}
        t.assert_equals(res, {'Ё'})
        -- the following calls should fail
        local exp_err = "Duplicate key exists in unique index \"pk\" "..
                        "in space \"t1\" with old tuple - "..
                        "[\"Ё\"] and new tuple - [\"е\"]"
        t.assert_error_msg_equals(exp_err, s.insert, s, {'е'})

        exp_err = "Duplicate key exists in unique index \"pk\" "..
                  "in space \"t1\" with old tuple - "..
                  "[\"Ё\"] and new tuple - [\"Е\"]"
        t.assert_error_msg_equals(exp_err, s.insert, s, {'Е'})

        exp_err = "Duplicate key exists in unique index \"pk\" "..
                  "in space \"t1\" with old tuple - "..
                  "[\"Ё\"] and new tuple - [\"ё\"]"
        t.assert_error_msg_equals(exp_err, s.insert, s, {'ё'})
        -- return single 'Ё'
        res = s:select{}
        t.assert_equals(res, {{'Ё'}})
        s:drop()
    end)
end
