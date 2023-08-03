local t = require('luatest')
local g = t.group('gh-8937')

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new{datadir = 'test/box-luatest/gh_8937_data'}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that indexes are recovered without errors like:
-- "Can't create or modify index 'pk' in space 'gh_8937_memtx':
-- hint is only reasonable with memtx tree index".
g.test_recovery = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.space.gh_8937_memtx.index[0].name, "pk")
        t.assert_equals(box.space.gh_8937_vinyl.index[0].name, "pk")
    end)
end
