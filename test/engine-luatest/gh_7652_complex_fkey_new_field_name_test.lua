local t = require('luatest')

local g = t.group('gh-7652', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    local server = require('luatest.server')
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
    end)
end)

-- Check that s2:insert{} doesn't fail with:
-- "Foreign key constraint 'one' failed: wrong local field name"
g.test_space_replace = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{'id', 'integer'}}
        local opts = {engine = engine, format = fmt}
        local s1 = box.schema.space.create('space1', opts)
        s1:create_index('i1')
        s1:insert{1}

        opts = {engine = engine}
        local s2 = box.schema.space.create('space2', opts)
        s2:create_index('i2')
        opts = {foreign_key = {one = {space = s1.id,
                                      field = {ext_id = 'id'}}}}
        fmt = {{name = 'id', type = 'integer'},
               {name = 'ext_id', type = 'integer'}}
        box.space._space:replace({s2.id, 1, 'space2', engine, 0, opts, fmt})
        s2:insert{11, 1}
    end, {engine})
end

-- Check that s2:insert{} doesn't fail with:
-- "Foreign key constraint 'one' failed: wrong local field name"
g.test_space_update = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{'id1', 'integer'}, {'id2', 'integer'}}
        local opts = {engine = engine, format = fmt}
        local s1 = box.schema.space.create('space1', opts)
        s1:create_index('i1', {parts={{1}, {2}}})
        s1:insert{1, 1}

        fmt = {{name = 'id', type = 'integer'},
               {name = 'ext_id1', type = 'integer'}}
        opts = {engine = engine, format = fmt}
        local s2 = box.schema.space.create('space2', opts)
        s2:create_index('i2')
        opts = {foreign_key = {one = {space = s1.id,
                                      field = {ext_id2 = 'id2',
                                               ext_id1 = 'id1'}}}}
        fmt = {{name = 'id', type = 'integer'},
               {name = 'ext_id1', type = 'integer'},
               {name = 'ext_id2', type = 'integer'}}
        box.space._space:update(s2.id, {{'=', 7, fmt}, {'=', 6, opts}})
        s2:insert{11, 1, 1}
    end, {engine})
end

-- Check that new_name doesn't leak through box.rollback()
g.test_dict_rollback = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{'id'}, {'old_name'}}
        local opts = {engine = engine, format = fmt}
        local s1 = box.schema.space.create('space1', opts)
        s1:create_index('pk')

        box.begin()
        s1:format({{'id'}, {'new_name'}})
        box.rollback()
        t.assert_error_msg_content_equals(
            "Tuple field 2 (old_name) required by space format is missing",
            function() s1:insert{1} end)
    end, {engine})
end
