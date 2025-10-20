local server = require('luatest.server')
local t = require('luatest')

local g = t.group('pagination_sysview', {{filter = false}, {filter = true}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_pagination_sysview = function(cg)
    cg.server:exec(function(filter)
        if filter then
            -- User with no space permissions so that only subset of spaces
            -- (I mean all the `sysview` spaces) is visible. Is needed to check
            -- how pagination works along with filtration.
            box.schema.user.create('testuser')
            box.session.su('testuser')
        end

        local s = box.space._vspace

        local tuples1
        local tuples2
        local tuples_offset
        local pos
        local last_tuple

        -- Test fullscan pagination
        pos = ""
        last_tuple = box.NULL
        local i = 0
        repeat
            tuples1, pos = s:select(nil,
                    {limit=2, fullscan=true, fetch_pos=true, after=pos})
            tuples2 = s:select(nil,
                    {limit=2, fullscan=true, after=last_tuple})
            last_tuple = tuples2[#tuples2]
            tuples_offset = s:select(nil,
                    {limit=2, fullscan=true, offset=i*2})
            t.assert_equals(tuples1, tuples_offset)
            t.assert_equals(tuples2, tuples_offset)
            i = i + 1
        until #tuples1 == 0

        -- Test pagination on range iterators
        local key = 289
        local iters = {'GE', 'GT', 'LE', 'LT'}
        for _, iter in pairs(iters) do
            pos = ""
            last_tuple = box.NULL
            i = 0
            repeat
                tuples1, pos = s:select(key,
                        {limit=2, iterator=iter, fetch_pos=true, after=pos})
                tuples2 = s:select(key,
                        {limit=2, iterator=iter, after=last_tuple})
                last_tuple = tuples2[#tuples2]
                tuples_offset = s:select(key,
                        {limit=2, iterator=iter, offset=i*2})
                t.assert_equals(tuples1, tuples_offset)
                t.assert_equals(tuples2, tuples_offset)
                i = i + 1
            until #tuples1 == 0
        end
        if filter then
            box.session.su('admin')
            box.schema.user.drop('testuser')
        end
    end, {cg.params.filter})
end
