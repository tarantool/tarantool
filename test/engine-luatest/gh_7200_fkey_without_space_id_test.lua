local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-7200-fkey-without-space-id',
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
        if box.space.space then box.space.space:drop() end
        if box.space.complex then box.space.complex:drop() end
        if box.space.filesystem then box.space.filesystem:drop() end
    end)
end)

-- Similar to test_foreign_key_primary in gh_6961_fkey_same_space_test.lua,
-- but without specifying space id in the format.
g.test_foreign_key_primary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id', type='unsigned'},
                     {name='parent_id', type='unsigned',
                      foreign_key={field='id'}}}
        local fs = box.schema.create_space('filesystem', {engine=engine,
                                                          format=fmt})
        t.assert_equals(fs:format(fs:format()), nil)
        fs:drop()

        local fmt = {{name='id', type='unsigned'},
                     {name='parent_id', type='unsigned',
                      foreign_key={fkey={field='id'}}}}
        local fs = box.schema.create_space('filesystem', {engine=engine})
        fs:format(fmt)
        t.assert_equals(fs:format(), fmt)
        fs:create_index('pk')
        fs:create_index('sk', {unique=false, parts={'parent_id'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'fkey' failed for field '2 (parent_id)': foreign tuple was not found",
            function() fs:insert{1, 0} end
        )
    end, {engine})
end

-- Test complex foreign key without specifying space id.
g.test_complex_foreign_key1 = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id1', type='unsigned'},
                     {name='id2', type='unsigned'}}
        local fkey = {field={id1='id1', id2='id2'}}
        local opts = {engine=engine, format=fmt, foreign_key=fkey}
        local space = box.schema.create_space('complex', opts)

        -- Note that 'space' field is not present in the table
        t.assert_equals(space.foreign_key,
                        {complex={field={id1='id1', id2='id2'}}})
    end, {engine})
end

g.test_complex_foreign_key2 = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id1', type='unsigned'},
                     {name='id2', type='unsigned'}}
        local fkey = {name={field={id1='id2', id2='id1'}}}
        local opts = {engine=engine, format=fmt, foreign_key=fkey}
        local space = box.schema.create_space('complex', opts)

        -- Note that 'space' field is not present in the table
        t.assert_equals(space.foreign_key,
                        {name={field={id1='id2', id2='id1'}}})
    end, {engine})
end

-- Test foreign key creation by inserting directly into box.space._space
g.test_foreign_key_direct_insert = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name = 'i', type = 'integer'},
                     {name = 'j', type = 'integer'},
                     {name = 'k', type = 'integer',
                      foreign_key = {k1 = {field = 'i'}}}}
        local opts = {foreign_key = {k2 = {field = {i = 'i', j = 'j'}}}}
        box.space._space:insert{512, 1, 'space', engine, 0, opts, fmt}
        t.assert_equals(box.space._space:get(512).flags, opts)
        t.assert_equals(box.space._space:get(512).format, fmt)
    end, {engine})
end
