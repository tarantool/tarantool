-- https://github.com/tarantool/tarantool/issues/13328
-- Complex foreign key treats a key field equal to 0 as NULL
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13328-complex-fkey-zero-key-field',
                  {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.child then box.space.child:drop() end
        if box.space.parent then box.space.parent:drop() end
    end)
end)

-- A referenced parent tuple with a zero key field cannot be deleted.
g.test_zero_key_field = function(cg)
    cg.server:exec(function(engine)
        local fmt = {{name = 'id1', type = 'unsigned'},
                     {name = 'id2', type = 'unsigned'}}
        local p = box.schema.create_space('parent',
                                          {engine = engine, format = fmt})
        p:create_index('pk', {parts = {{1}, {2}}})
        p:insert{0, 1}
        p:insert{5, 6}

        fmt = {{name = 'id', type = 'unsigned'},
               {name = 'e1', type = 'unsigned'},
               {name = 'e2', type = 'unsigned'}}
        local fkey = {space = 'parent', field = {e1 = 'id1', e2 = 'id2'}}
        local c = box.schema.create_space('child', {engine = engine,
                                                   format = fmt,
                                                   foreign_key = fkey})
        c:create_index('pk')
        c:create_index('sk', {parts = {{2}, {3}}, unique = false})
        c:insert{10, 0, 1}
        c:insert{20, 5, 6}

        local msg = "Foreign key 'parent' integrity check failed: " ..
                    "tuple is referenced"
        t.assert_error_msg_content_equals(msg, function() p:delete{5, 6} end)
        t.assert_error_msg_content_equals(msg, function() p:delete{0, 1} end)
        t.assert_equals(p:select{}, {{0, 1}, {5, 6}})
    end, {cg.params.engine})
end

-- A parent tuple with a NULL key field is not referenced and is deletable.
g.test_null_key_field = function(cg)
    cg.server:exec(function(engine)
        local parts = {{2, 'string', is_nullable = true},
                       {3, 'string', is_nullable = true}}
        local fmt = {{name = 'id', type = 'unsigned'},
                     {name = 'k1', type = 'string', is_nullable = true},
                     {name = 'k2', type = 'string', is_nullable = true}}
        local p = box.schema.create_space('parent',
                                          {engine = engine, format = fmt})
        p:create_index('pk')
        p:create_index('sk', {parts = parts})

        fmt = {{name = 'id', type = 'unsigned'},
               {name = 'e1', type = 'string', is_nullable = true},
               {name = 'e2', type = 'string', is_nullable = true}}
        local fkey = {space = 'parent', field = {e1 = 'k1', e2 = 'k2'}}
        local c = box.schema.create_space('child', {engine = engine,
                                                   format = fmt,
                                                   foreign_key = fkey})
        c:create_index('pk')
        c:create_index('sk', {parts = parts, unique = false})

        p:insert{1, 'a', box.NULL}
        p:delete{1}
        -- The key does not fit into the static key buffer.
        p:insert{2, box.NULL, string.rep('x', 5000)}
        p:delete{2}
        t.assert_equals(p:select{}, {})
    end, {cg.params.engine})
end
