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
    end)
end
