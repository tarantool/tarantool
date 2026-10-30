local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{engine = 'vinyl'}, {engine = 'memtx'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        -- Checks that rebuild of index (that is passed as `func`) is disabled.
        local function check_rebuild_disabled(func, ...)
            t.assert_error_covers({
                type = "ClientError",
                name = "UNSUPPORTED",
                message = "Tarantool does not support non-trivial alter of " ..
                          "primary index along with rebuild of dependent " ..
                          "secondary indexes",
            }, func, ...)
        end
        rawset(_G, 'check_rebuild_disabled', check_rebuild_disabled)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')

        local s = box.schema.create_space('test', {engine = engine})
        s:create_index('pk')

        fiber.set_slice(30)
        box.begin()
        for i = 1, 2000 do
            s:replace{i, i}
        end
        box.commit()
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_unique_index = function(cg)
    cg.server:exec(function(engine)
        local check_rebuild_disabled = rawget(_G, 'check_rebuild_disabled')

        local s = box.space.test
        s:create_index('sk', {unique = true})

        if engine == 'memtx' then
            -- Unique non-nullable index in memtx doesn't depend on PK.
            s.index.pk:alter({parts = {2, 'unsigned'}})
        else
            assert(engine == 'vinyl')
            check_rebuild_disabled(s.index.pk.alter, s.index.pk,
                                   {parts = {2, 'unsigned'}})
        end
    end, {cg.params.engine})
end

g.test_non_unique_index = function(cg)
    cg.server:exec(function()
        local check_rebuild_disabled = rawget(_G, 'check_rebuild_disabled')

        local s = box.space.test
        s:create_index('sk', {unique = false})

        check_rebuild_disabled(s.index.pk.alter, s.index.pk,
                               {parts = {2, 'unsigned'}})
    end)
end

g.test_nullable_index = function(cg)
    cg.server:exec(function()
        local check_rebuild_disabled = rawget(_G, 'check_rebuild_disabled')

        local s = box.space.test
        s:create_index('sk', {parts = {2, 'unsigned', is_nullable = true}})
        check_rebuild_disabled(s.index.pk.alter, s.index.pk,
                               {parts = {2, 'unsigned'}})
    end)
end
