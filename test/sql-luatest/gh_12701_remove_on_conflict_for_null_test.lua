local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-12701: remove the non-working ON CONFLICT clause for the NULL property
-- in the column definition in the CREATE TABLE statement.
--
g.test_12701_remove_on_conflict_for_null = function(cg)
    cg.server:exec(function()
        local sql = [[
            CREATE TABLE t(i INT PRIMARY KEY, a INT NULL ON CONFLICT ABORT);
        ]]
        local _, err = box.execute(sql)
        t.assert_str_contains(err.message, "keyword 'ON' is reserved.")
    end)
end
