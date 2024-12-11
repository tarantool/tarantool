local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-10132')

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        local msgpack = require('msgpack')
        rawset(_G, 'conn', require('net.box').connect(box.cfg.listen))
        rawset(_G, 'do_sql', function(...)
            local rows = _G.conn:execute(...).rows
            t.assert_equals(#rows, 1)
            t.assert_equals(#rows[1], 1)
            return rows[1][1]
        end)
        rawset(_G, 'make_mp', function(...)
            return msgpack.object_from_raw(...)
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_bind = function(cg)
    cg.server:exec(function()
        t.assert_equals(_G.do_sql('SELECT ?', {_G.make_mp('\xd0\x01')}), 1)
    end)
end
