local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_no_error_on_too_many_indexes'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_no_error_on_too_many_indexes = function()
    g.server:exec(function()
        local s = "CREATE TABLE add (c0 INT PRIMARY KEY"
        for i = 1,128 do
            s = s .. ", c" .. i .. " INT UNIQUE"
        end
        s = s .. ");"
        local _, err = box.execute(s)
        local res = [[Can't create or modify index 'unique_unnamed_ADD_129' ]]..
                    [[in space 'ADD': index id too big]]
        t.assert_equals(err.message, res)
    end)
end
