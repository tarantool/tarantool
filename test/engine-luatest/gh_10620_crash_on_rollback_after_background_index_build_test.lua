local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_crash_on_wal_failure = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, i})
        end
        local ch = fiber.channel(1)
        fiber.create(function()
            box.begin()
            space:create_index('sk')
            ch:put(true)
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
            box.commit()
        end)
        fiber.create(function()
            box.begin()
            for i = 1, 100 do
                space:replace{i, i}
            end
            ch:put(true)
            box.commit()
        end)
        ch:get()
        ch:get()
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end, {cg.params.engine})
end

-- Cover the case with manual rollback. Bump the space cache version
-- in order to check that implementation doesn't rely on it.
g.test_manual_rollback = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, i})
        end
        local select_result = space:select{}
        local ddl = fiber.create(function()
            box.begin()
            space:create_index('sk')
            box.commit()
        end)
        ddl:set_joinable(true)
        fiber.create(function()
            box.begin()
            for i = 1, 100 do
                space:replace{i, i + 100}
            end
            -- Bump space cache version
            box.schema.space.create('test2')
            box.rollback()
        end)
        local ok, err = ddl:join()
        t.assert(ok, err)
        t.assert_equals(space.index.sk:select{}, select_result)
    end, {cg.params.engine})
end
