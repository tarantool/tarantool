local t = require('luatest')
local g = t.group('gh-7149')
local server = require('luatest.server')

g.after_each(function()
    g.server:drop()
end)

g.before_test('test_schema_access', function()
    g.server = server:new{alias = 'master'}
    g.server:start()
end)

-- Check that get_version() from upgrade.lua can be executed by any user without
-- getting an error: Read access to space '_schema' is denied for user 'user'.
g.test_schema_access = function()
    g.server:exec(function()
        box.cfg()
        box.schema.user.create('user')
        box.session.su('user')
        box.schema.upgrade()
    end)
end
