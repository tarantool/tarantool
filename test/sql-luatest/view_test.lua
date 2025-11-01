-- Make sure, that it is possible to create a VIEW which
-- refers to "_v" space, i.e. to sysview engine.
-- Before gh-4111 was fixed, attempt to create such a view
-- failed due to lack of format in a space with sysview
-- engine.


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

g.test_4111_format_in_sysview = function()
    g.server:exec(function()
        t.assert(box.space._vspace.index[1]:count(1) > 0)

        box.execute([[CREATE VIEW t AS SELECT "name" FROM "_vspace" v;]])
        t.assert(box.space.t ~= nil)
        box.execute([[SELECT * FROM t WHERE "name" = 't';]])
        box.execute([[DROP VIEW t;]])
    end)
end
