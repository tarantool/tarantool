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
    end)
end)

g.test_read_upsert = function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:insert({100})
        box.snapshot()
        s:upsert({200}, {})
        s:upsert({300}, {})
        s:upsert({400}, {})
        box.snapshot()
        t.assert_equals(s:select({500}, {iterator = 'lt', fullscan = true}),
                        {{400}, {300}, {200}, {100}})
    end)
end

g.test_read_cache = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local timeout = 30
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:replace({500})
        box.snapshot()
        s:upsert({200}, {})
        box.snapshot()
        s:replace({100})
        s:replace({300})
        s:replace({400})
        box.snapshot()
        local c = fiber.channel()
        fiber.new(function()
            local _
            local result = {}
            local gen, param, state = s:pairs({400}, {iterator = 'lt'})
            _, result[#result + 1] = gen(param, state) -- {300}
            c:put(true)
            c:get()
            _, result[#result + 1] = gen(param, state) -- {200}
            _, result[#result + 1] = gen(param, state) -- {100}
            _, result[#result + 1] = gen(param, state) -- eof
            c:put(result)
        end)
        t.assert(c:get(timeout))
        s:replace({350, 'x'}) -- send the iterator to a read view
        s:replace({150, 'x'}) -- must be invisible to the iterator
        -- Add the interval connecting the tuple {100} visible from the read
        -- view with the tuple {150, 'x'} invisible from the read view to
        -- the cache.
        t.assert_equals(s:select({100}, {iterator = 'ge', limit = 2}),
                        {{100}, {150, 'x'}})
        c:put(true, timeout)
        t.assert_equals(c:get(timeout), {{300}, {200}, {100}})
    end)
end
