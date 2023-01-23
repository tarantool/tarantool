-- https://github.com/tarantool/tarantool/issues/7046
-- Complex foreign keys do not work with nullable fields
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-7046-complex-fkey-nullable-test',
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
        if box.space.space2 then box.space.space2:drop() end
        if box.space.space1 then box.space.space1:drop() end
        if box.space.filesystem then box.space.filesystem:drop() end
    end)
end)

-- Test with nullable complex foreign key by primary index
g.test_complex_fkey_primary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id1', type='unsigned'},
                     {name='id2', type='unsigned'}}
        local opts = {engine=engine, format=fmt}
        local s1 = box.schema.create_space('space1', opts)
        s1:create_index('pk', {parts={{1}, {2}}})
        s1:insert{1, 2}

        local fmt = {{name='id', type='unsigned'},
                     {name='ext_id1', type='unsigned', is_nullable=true},
                     {name='ext_id2', type='unsigned', is_nullable=true}}
        local fkey = {space=s1.id, field={[2]=1, [3]=2}}
        local opts = {engine=engine, format=fmt, foreign_key=fkey}
        local s2 = box.schema.create_space('space2', opts)
        s2:create_index('pk')

        s2:insert{11}
        s2:insert{22, 1, 2}
        s2:insert{33, box.NULL, box.NULL, ''}

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'space1' failed: extract key failed",
            function() s2:insert{55, 1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'space1' failed: wrong key type",
            function() s2:insert{66, box.NULL, 2} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'space1' failed: wrong key type",
            function() s2:insert{77, 1, box.NULL, ''} end
        )
    end, {engine})
end

-- Test with nullable complex foreign key by secondary index and some variations
g.test_complex_fkey_secondary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id', type='string'},
                     {name='id1', type='string'},
                     {name='id2', type='string'}}
        local opts = {engine=engine, format=fmt}
        local s1 = box.schema.create_space('space1', opts)
        s1:create_index('pk')
        s1:create_index('sk', {parts={{'id1'}, {'id2'}}})
        s1:insert{'0', '1', '2'}

        local fmt = {{name='id', type='string'},
                     {name='data1', type='string', is_nullable=true},
                     {name='e_id1', type='string', is_nullable=true},
                     {name='data2', type='string', is_nullable=true},
                     {name='e_id2', type='string', is_nullable=true}}
        local fkey = {key = {space='space1', field={e_id2='id2', e_id1='id1'}}}
        local opts = {engine=engine, format=fmt, foreign_key=fkey}
        local s2 = box.schema.create_space('space2', opts)
        s2:create_index('pk')

        s2:insert{'id1', 'dd'}
        s2:insert{'id2', 'dd', box.NULL, 'dd'}
        s2:insert{'id3', box.NULL, '1', 'dd', '2'}
        s2:insert{'id4', 'dd', box.NULL, 'dd', box.NULL}

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'key' failed: extract key failed",
            function() s2:insert{'id5', 'dd', '1'} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'key' failed: wrong key type",
            function() s2:insert{'id5', 'dd', box.NULL, 'dd', '2'} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'key' failed: wrong key type",
            function() s2:insert{'id5', box.NULL, '1', box.NULL, box.NULL, ''} end
        )
    end, {engine})
end

-- Test with nullable complex foreign key pointing to the same space
g.test_complex_fkey_self = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='disk', type='string'},
                     {name='dir', type='string'},
                     {name='parent_disk', type='string', is_nullable=true},
                     {name='parent_dir', type='string', is_nullable=true}}
        local fkey = {space='filesystem',
                      field={parent_disk='disk', parent_dir='dir'}}
        local opts = {engine=engine, format=fmt, foreign_key=fkey}
        local fs = box.schema.create_space('filesystem', opts)
        fs:create_index('pk', {parts={{1}, {2}}})
        fs:create_index('sk', {parts={{3}, {4}}, unique=false})

        fs:insert{'C', ''}
        fs:insert{'C', 'Users', 'C', ''}
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'filesystem' failed: extract key failed",
            function() fs:insert{'C', 'Windows', 'C'} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'filesystem' failed: wrong key type",
            function() fs:insert{'C', 'Windows', box.NULL, ''} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key 'filesystem' integrity check failed: tuple is referenced",
            function() fs:delete{'C', ''} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'filesystem' failed: foreign tuple was not found",
            function() fs:insert{'D', 'Users', 'D', ''} end
        )
    end, {engine})
end
