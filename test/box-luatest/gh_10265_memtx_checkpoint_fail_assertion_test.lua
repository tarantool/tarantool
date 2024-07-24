local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_memtx_checkpoint_fail_assertion = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:replace{0, 0}
        -- Set error injection to iterator creation to fail
        -- memtx checkpoint right from the start.
        box.error.injection.set('ERRINJ_INDEX_ITERATOR_NEW', true)
        local ok = pcall(box.snapshot)
        box.error.injection.set('ERRINJ_INDEX_ITERATOR_NEW', false)
        t.assert_equals(ok, false, "Snapshot must fail")
    end)
end
