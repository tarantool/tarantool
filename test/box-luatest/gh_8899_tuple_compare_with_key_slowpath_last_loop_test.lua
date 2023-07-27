local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-8899-tuple-compare-with-key-slowpath-last-loop')

g.before_all(function()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_tuple_compare_with_key_slowpath_last_loop = function()
    g.server:exec(function()
        local ffi = require('ffi')

        local function double(n)
            return ffi.cast('double', n)
        end

        local s = box.schema.space.create('test', {engine = 'memtx'})
        s:create_index('pk')
        local sk = s:create_index('sk', {parts = {
                {1, 'unsigned'},
                {2, 'number', is_nullable = true},
                {3, 'number', is_nullable = true}
        }})

        -- 1-byte unsigned in DB, 8-byte double in request.
        s:replace({1, 2})
        t.assert_equals(sk:select({1, double(2), box.NULL}, {iterator = 'EQ'}),
                        {{1, 2}})

        -- 8-byte double in DB, 1-byte unsigned in request.
        s:replace({1, double(3)})
        t.assert_equals(sk:select({1, 3, box.NULL}, {iterator = 'EQ'}),
                        {{1, 3}})
    end)
end
