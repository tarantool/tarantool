local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.user.create('criminal')
        box.schema.user.passwd('criminal', '123')
    end)
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_no__collation_in_public_access = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        -- 2 is 'public' role id
        local r = box.space._priv:select{2, 'space', box.space._collation.id}
        t.assert_equals(#r, 0)
    end)
end

g.test_no__collation_over_netbox = function(cg)
    local login = {user = 'criminal', password = '123'}
    local conn = require('net.box').connect(cg.server.net_box_uri, login)

    t.assert_equals(conn.space._collation, nil)
end
