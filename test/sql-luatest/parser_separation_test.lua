local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

--
-- Make sure that the ALTER TABLE RENAME statement is processed only after
-- successful parsing.
--
g.test_table_rename_parsing = function()
    g.server:exec(function()
        local _, err = box.execute([[ALTER TABLE t RENAME TO t1 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")
    end)
end

--
-- Make sure that the ALTER TABLE DROP CONSTRAINT statement is processed only
-- after successful parsing.
--
g.test_drop_constraint_parsing = function()
    g.server:exec(function()
        local _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT t1 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")
    end)
end

--
-- Make sure that the DROP INDEX statement is processed only after successful
-- parsing.
--
g.test_drop_index_parsing = function()
    g.server:exec(function()
        local _, err = box.execute([[DROP INDEX i1 on t1 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")
    end)
end

--
-- Make sure that the DROP VIEW statement is processed only after successful
-- parsing.
--
g.test_drop_view_parsing = function()
    g.server:exec(function()
        local _, err = box.execute([[DROP VIEW v 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")
    end)
end
