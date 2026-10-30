local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13481-memtx-func-index-old-tuple-failure')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that a replace failing on the function call for the replaced tuple
-- leaves the functional index as it was.
g.test_func_index_old_tuple_failure = function(cg)
    cg.server:exec(function()
        box.schema.func.create('f', {
            body = [[
                function(tuple)
                    if rawget(_G, 'fail') and tuple[2] == 'a' then
                        error('boom')
                    end
                    return {tuple[1]}
                end
            ]],
            is_deterministic = true,
        })
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('sk', {parts = {{1, 'unsigned'}}, func = 'f',
                              unique = false})
        s:insert{1, 'a'}
        rawset(_G, 'fail', true)
        t.assert_error_msg_content_equals(
            "Failed to build a key for functional index 'sk' of space 's': " ..
            "can't evaluate function", s.replace, s, {1, 'b'})
        collectgarbage()
        t.assert_equals(s:select(), {{1, 'a'}})
        t.assert_equals(s.index.sk:select(), {{1, 'a'}})
        rawset(_G, 'fail', nil)
        s:replace{1, 'b'}
        t.assert_equals(s.index.sk:select(), {{1, 'b'}})
    end)
end
