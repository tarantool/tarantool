local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
        box.error.injection.set('ERRINJ_VY_STMT_DECODE_COUNTDOWN', -1)
    end)
end)

g.test_compaction_read_error = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')

        -- Delay compaction and create a few runs.
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)
        for _ = 1, 5 do
            box.begin()
            for k = 1, 100 do
                s:replace({k})
            end
            box.commit()
            box.snapshot()
        end

        -- Make compaction fail while decoding a random statement.
        box.error.injection.set('ERRINJ_VY_STMT_DECODE_COUNTDOWN',
                                math.random(500))

        -- Resume compaction and wait for it to fail.
        box.stat.reset()
        t.assert_equals(box.stat.vinyl().scheduler.tasks_failed, 0)
        s.index.pk:compact()
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_ge(box.stat.vinyl().scheduler.tasks_failed, 1)
        end)
    end)
end
