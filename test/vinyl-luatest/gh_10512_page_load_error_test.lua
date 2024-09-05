local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            -- Disable cache to force reads from disk.
            vinyl_cache = 0,
            vinyl_max_tuple_size = 1024 * 1024,
        },
    })
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
        box.cfg{vinyl_max_tuple_size = 1024 * 1024}
    end)
end)

g.test_page_load_error = function(cg)
    cg.server:exec(function()
        local digest = require('digest')
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk', {page_size = 1024})
        for i = 1, 50 do
            -- Use random padding to make compression ineffective.
            -- With constant padding, all the tuple could end up
            -- in a single page.
            s:insert({i, digest.urandom(128)})
        end
        -- Dumps tuples to disk.
        box.snapshot()
        box.cfg{vinyl_max_tuple_size = 128}
        for _, iterator in ipairs({'ge', 'gt', 'le', 'lt', 'eq', 'req'}) do
            for key = 1, 50 do
                -- With key = 1 and iterator = 'lt', the read iterator will
                -- figure out that no page can store requested tuples by
                -- looking at the first page's min key and won't load any
                -- pages.
                if key > 1 or iterator ~= 'lt' then
                    t.assert_error_covers({
                        type = 'ClientError',
                        code = box.error.VINYL_MAX_TUPLE_SIZE,
                    }, s.count, s, key, {iterator = iterator})
                end
            end
        end
    end)
end
