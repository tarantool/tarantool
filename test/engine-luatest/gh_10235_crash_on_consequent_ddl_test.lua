local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- Reproducer from the issue.
g.test_crash_on_consequent_alters = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, 2})
        end
        local ch = fiber.channel(1)
        fiber.create(function()
            box.begin()
            space:create_index('sk')
            -- Inject WAL error only after index is built in order
            -- to allow vinyl use WAL during the index build
            box.error.injection.set('ERRINJ_WAL_WRITE', true)
            ch:put(true)
            box.commit()
        end)
        t.assert(ch:get(10))
        local function alter_space()
            space:format({{'one', 'unsigned'}, {'two', 'unsigned'}})
        end
        t.assert_error_msg_content_equals(
            "Can't modify space '512': the space was concurrently modified",
            alter_space)
    end, {cg.params.engine})
end
