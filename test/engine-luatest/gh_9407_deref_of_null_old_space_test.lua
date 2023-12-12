local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-9407', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that Tarantool doesn't crash in the following scenario:
g.test_null_dereference = function(cg)
    cg.server:exec(function(engine)
        local space_id = 512
        -- 1. Disable the `on_replace_dd_space' core trigger.
        box.space._space:run_triggers(false)
        -- 2. New space is inserted into `_space', but not into the space cache.
        box.schema.create_space('test', {engine = engine, id = space_id})
        -- 3. Enable `on_replace_dd_space' back.
        box.space._space:run_triggers(true)
        -- 4. Try to drop the space.
        box.space._space:delete({space_id})
    end, {cg.params.engine})
end
