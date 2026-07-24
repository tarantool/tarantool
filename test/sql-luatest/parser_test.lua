local server = require('luatest.server')
local t = require('luatest')

local g = t.group('parser')

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- gh-5485: Check changed errors for `ALTER TABLE` rules.
g.test_alter_table_reserved_keyword_error = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (id INT PRIMARY KEY, a INT);]])

        local exp_err = "Syntax error at line 1 near 'REFERENCES'"
        local queries = {
            [[ALTER TABLE t ADD COLUMN REFERENCES INT;]],
            [[ALTER TABLE t ADD REFERENCES INT;]],
            [[ALTER TABLE t ADD CONSTRAINT fk FOREIGN KEY REFERENCES t(id);]],
            [[ALTER TABLE t ADD CONSTRAINT ck CHECK REFERENCES;]],
            [[ALTER TABLE t ADD CONSTRAINT uq UNIQUE REFERENCES;]],
            [[ALTER TABLE t ADD CONSTRAINT pk PRIMARY KEY REFERENCES;]],
        }
        for _, query in ipairs(queries) do
            local _, err = box.execute(query)
            t.assert_equals(err.message, exp_err)
        end

        box.space.t:drop()
    end)
end

-- Make sure that syntax errors occur before semantic errors.
g.test_syntax_errors = function(cg)
    cg.server:exec(function()
        local _, err = box.execute([[SELECT i FROM t 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")

        _, err = box.execute([[DROP TABLE t 2;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '2'")

        _, err = box.execute([[DROP VIEW v 3;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '3'")

        _, err = box.execute([[DROP TRIGGER t 4;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '4'")

        _, err = box.execute([[DROP INDEX I ON t 5;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '5'")

        _, err = box.execute([[ALTER TABLE t RENAME TO t1 6;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '6'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT c 7;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '7'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT c FOREIGN KEY 8;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '8'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT c PRIMARY KEY 9;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '9'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT c UNIQUE 0;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '0'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT c CHECK 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT a.c 2;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '2'")

        _, err = box.execute("ALTER TABLE t DROP CONSTRAINT a.c FOREIGN KEY 3;")
        t.assert_equals(err.message, "Syntax error at line 1 near '3'")

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT a.c CHECK 4;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '4'")

        _, err = box.execute([[ALTER TABLE t ADD CONSTRAINT c UNIQUE (i) 5;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '5'")

        _, err = box.execute([[ALTER TABLE t ADD CONSTRAINT c
                               PRIMARY KEY (i) 6;]])
        t.assert_equals(err.message, "Syntax error at line 2 near '6'")

        _, err = box.execute([[ALTER TABLE t ADD CONSTRAINT c
                               FOREIGN KEY (i) REFERENCES t1(i) 7;]])
        t.assert_equals(err.message, "Syntax error at line 2 near '7'")

        _, err = box.execute([[ALTER TABLE t ADD CONSTRAINT c
                               CHECK (i > 10) 8;]])
        t.assert_equals(err.message, "Syntax error at line 2 near '8'")

        _, err = box.execute([[ALTER TABLE t ADD COLUMN i INT 9;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '9'")

        _, err = box.execute([[CREATE TABLE t(i INT) 0;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '0'")
    end)
end
