local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_sequence_name = function(cg)
    cg.server:exec(function()
        box.schema.user.create('u')
        box.schema.sequence.create('seq')
        box.schema.user.grant('u', 'write', 'sequence', 'seq')
        t.assert_equals(box.session.su('u', box.schema.user.info, 'u'),
                        {{"execute", "role", "public"},
                         {"write", "sequence", "seq"},
                         {"session,usage", "universe", ""},
                         {"alter", "user", "u"}})
    end)
end
