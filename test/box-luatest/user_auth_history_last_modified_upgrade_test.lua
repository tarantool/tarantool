local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        datadir = 'test/box-luatest/upgrade/2.10.4',
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_upgrade = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        t.assert_equals(box.space._user:select(), {
            {0, 1, 'guest', 'user',
             {['chap-sha1'] = 'vhvewKp0tNyweZQ+cFKAlsyphfg='}},
            {1, 1, 'admin', 'user', {}},
            {2, 1, 'public', 'role', {}},
            {3, 1, 'replication', 'role', {}},
            {31, 1, 'super', 'role', {}},
            {32, 1, 'eve', 'user', {}},
            {33, 1, 'bob', 'user',
             {['chap-sha1'] = 'FOZVZ6vbUTXQz9mnCzAywXmknuc='}},
            {34, 1, 'test', 'role', {}},
        })
        box.schema.user.create('alice')
        box.schema.user.create('sarah', {password = 'SARAH'})
        box.schema.user.passwd('bob', 'BOB')
        box.schema.role.create('dev')
        t.assert_equals(box.space._user:select(), {
            {0, 1, 'guest', 'user',
             {['chap-sha1'] = 'vhvewKp0tNyweZQ+cFKAlsyphfg='}},
            {1, 1, 'admin', 'user', {}},
            {2, 1, 'public', 'role', {}},
            {3, 1, 'replication', 'role', {}},
            {31, 1, 'super', 'role', {}},
            {32, 1, 'eve', 'user', {}},
            {33, 1, 'bob', 'user',
             {['chap-sha1'] = 'Ll5w6uuDmXlEaz2b8kmjHZu1SLg='}, {},
             box.space._user:get(33)[7]},
            {34, 1, 'test', 'role', {}},
            {35, 0, 'alice', 'user', {}, {},
             box.space._user:get(35)[7]},
            {36, 0, 'sarah', 'user',
             {['chap-sha1'] = '973rCIFsYhe7gupdgOPCSJoPRNU='}, {},
             box.space._user:get(36)[7]},
            {37, 0, 'dev', 'role', {}, {},
             box.space._user:get(37)[7]},
        })
        local time = fiber.time()
        local margin = 60
        t.assert_almost_equals(box.space._user:get(33)[7], time, margin)
        t.assert_almost_equals(box.space._user:get(35)[7], time, margin)
        t.assert_almost_equals(box.space._user:get(36)[7], time, margin)
        t.assert_almost_equals(box.space._user:get(37)[7], time, margin)
        box.schema.upgrade()
        local format = {
            {name = "id", type = "unsigned"},
            {name = "owner", type = "unsigned"},
            {name = "name", type = "string"},
            {name = "type", type = "string"},
            {name = "auth", type = "map"},
            {name = "auth_history", type = "array"},
            {name = "last_modified", type = "unsigned"},
        }
        t.assert_equals(box.space._user:format(), format)
        t.assert_equals(box.space._vuser:format(), format)
        t.assert_equals(box.space._user:select(), {
            {0, 1, 'guest', 'user',
             {['chap-sha1'] = 'vhvewKp0tNyweZQ+cFKAlsyphfg='}, {}, 0},
            {1, 1, 'admin', 'user', {}, {}, 0},
            {2, 1, 'public', 'role', {}, {}, 0},
            {3, 1, 'replication', 'role', {}, {}, 0},
            {31, 1, 'super', 'role', {}, {}, 0},
            {32, 1, 'eve', 'user', {}, {}, 0},
            {33, 1, 'bob', 'user',
             {['chap-sha1'] = 'Ll5w6uuDmXlEaz2b8kmjHZu1SLg='}, {},
             box.space._user:get(33)[7]},
            {34, 1, 'test', 'role', {}, {}, 0},
            {35, 0, 'alice', 'user', {}, {},
             box.space._user:get(35)[7]},
            {36, 0, 'sarah', 'user',
             {['chap-sha1'] = '973rCIFsYhe7gupdgOPCSJoPRNU='}, {},
             box.space._user:get(36)[7]},
            {37, 0, 'dev', 'role', {}, {},
             box.space._user:get(37)[7]},
        })
    end)
end
