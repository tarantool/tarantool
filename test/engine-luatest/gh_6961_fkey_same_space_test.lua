-- https://github.com/tarantool/tarantool/issues/6961
-- Can't create space with foreign key pointing to itself
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-6961-fkey-same-space-test',
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
        if box.space.tasks then box.space.tasks:drop() end
        if box.space.filesystem then box.space.filesystem:drop() end
        if box.space.population then box.space.population:drop() end
        if box.func.check_parent then box.func.check_parent:drop() end
    end)
end)

-- Test with foreign key to the same space by primary index
-- and a key from the other space.
g.test_foreign_key_primary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id', type='unsigned'},
                     {name='name', type='string'},
                     {name='parent_id', type='unsigned', is_nullable=true,
                      foreign_key={fkey={space='filesystem', field='id'}}}
                    }
        local fs = box.schema.create_space('filesystem', {engine=engine,
                                                          format=fmt})
        fs:create_index('pk')
        fs:create_index('sk', {unique=false, parts={'parent_id'}})
        fs:insert{0, 'root'}
        fs:insert{1, 'bin', 0}
        fs:insert{2, 'usr', 0}
        fs:insert{3, 'etc', 0}
        fs:insert{10, 'dd', 1}
        fs:insert{11, 'df', 1}
        fs:insert{20, 'bin', 2}
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'fkey' failed for field '3 (parent_id)': foreign tuple was not found",
            function() fs:insert{8, 'boot', 666} end
        )

        local fmt = {{name='pid', type='unsigned'},
                     {name='comm_id', type='unsigned',
                     foreign_key={space='filesystem', field='id'}}
                    }
        local tasks = box.schema.create_space('tasks', {engine=engine,
                                                        format=fmt})
        tasks:create_index('pk')
        tasks:create_index('sk', {parts={'comm_id'}})
        tasks:insert{1234, 11}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local fs = box.space.filesystem

        fs.index.sk:drop()
        fs:create_index('sk', {unique=false, parts={'parent_id'}})

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'fkey' failed for field '3 (parent_id)': foreign tuple was not found",
            function() fs:replace{10, 'dd', 404} end
        )
        fs:replace{10, 'dd', 20} -- mv /bin/dd /usr/bin/dd

        t.assert_error_msg_content_equals(
            "Foreign key 'fkey' integrity check failed: tuple is referenced",
            function() fs:delete{2} end
        )
        fs:delete{10} -- rm /usr/bin/dd
        fs:delete{20} -- rm /usr/bin
        fs:delete{2} -- Now /usr can be deleted

        t.assert_error_msg_content_equals(
            "Foreign key 'filesystem' integrity check failed: tuple is referenced",
            function() fs:delete{11} end
        )
    end, {engine})
end

-- Test with fkey to the same space by secondary index and some variations
g.test_foreign_key_secondary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fn_check_parent = "function(field) return field:sub(1, 1) == '/' end"
        box.schema.func.create('check_parent', {language='LUA',
                                                is_deterministic=true,
                                                body=fn_check_parent})
        local fmt = {{name='id', type='unsigned'},
                     {name='path', type='string'},
                     {name='parent', type='string', is_nullable=true,
                      constraint='check_parent',
                      foreign_key={space='filesystem', field='path'}}
                    }
        local fs = box.schema.create_space('filesystem', {engine=engine,
                                                          format=fmt})
        fs:create_index('id')
        fs:create_index('path', {parts={'path'}})
        fs:create_index('parent', {unique=false, parts={'parent'}})
        fs:insert{0, '/'}
        fs:insert{1, '/bin', '/'}
        fs:insert{2, '/usr', '/'}
        fs:insert{3, '/etc', '/'}
        fs:insert{4, '/bin/dd', '/bin'}
        fs:insert{5, '/bin/df', '/bin'}
        fs:insert{6, '/usr/bin', '/usr'}
        fs:insert{7, '???'}
        t.assert_error_msg_content_equals(
            "Check constraint 'check_parent' failed for field '3 (parent)'",
            function() fs:insert{8, '/boot', '???'} end
        )

        fs.index.parent:drop()
        fs:create_index('parent', {unique=false, parts={'parent'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local fs = box.space.filesystem.index.path

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'filesystem' failed for field '3 (parent)': foreign tuple was not found",
            function() fs:update('/bin/dd', {{'=', 'parent', '/nanan'}}) end
        )
        fs:update('/bin/dd', {{'=', 'parent', '/usr/bin'}, {'=', 'path', '/usr/bin/dd'}})

        t.assert_error_msg_content_equals(
            "Foreign key 'filesystem' integrity check failed: tuple is referenced",
            function() fs:delete{'/usr/bin'} end
        )
        fs:delete{'/usr/bin/dd'}
        fs:delete{'/usr/bin'}
    end, {engine})
end

-- Test with 2 foreign keys by primary index
g.test_two_fkeys_primary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id', type='unsigned'},
                     {name='name', type='string'},
                     {name='mother', type='unsigned', is_nullable=true,
                      foreign_key={fkey1={space='population', field='id'}}},
                     {name='father', type='unsigned', is_nullable=true,
                      foreign_key={fkey2={space='population', field='id'}}}
                    }
        local s = box.schema.create_space('population', {engine=engine,
                                                         format=fmt})
        s:create_index('pk')
        s:create_index('sk1', {unique=false, parts={'mother'}})
        s:create_index('sk2', {unique=false, parts={'father'}})
        s:insert{0, 'Eve'}
        s:insert{1, 'Adam'}
        s:insert{2, 'Cain', 0, 1}
        s:insert{3, 'Abel', 0, 1}

        t.assert_error_msg_content_equals(
            "Foreign key 'fkey1' integrity check failed: tuple is referenced",
            function() s:delete{0} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key 'fkey2' integrity check failed: tuple is referenced",
            function() s:delete{1} end
        )
    end, {engine})
end

-- Test with 2 foreign keys by secondary index
g.test_two_fkeys_secondary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{name='id', type='unsigned'},
                     {name='name', type='string'},
                     {name='mother', type='string', is_nullable=true,
                      foreign_key={fkey1={space='population', field='name'}}},
                     {name='father', type='string', is_nullable=true,
                      foreign_key={fkey2={space='population', field='name'}}}
                    }
        local s = box.schema.create_space('population', {engine=engine,
                                                         format=fmt})
        s:create_index('pk')
        s:create_index('name', {parts={'name'}})
        s:create_index('sk1', {unique=false, parts={'mother'}})
        s:create_index('sk2', {unique=false, parts={'father'}})
        s:insert{0, 'Eve'}
        s:insert{1, 'Adam'}
        s:insert{2, 'Cain', 'Eve', 'Adam'}
        s:insert{3, 'Abel', 'Eve', 'Adam'}

        t.assert_error_msg_content_equals(
            "Foreign key 'fkey1' integrity check failed: tuple is referenced",
            function() s:replace{0, 'eve'} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key 'fkey2' integrity check failed: tuple is referenced",
            function() s:replace{1, 'adam'} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key 'fkey2' integrity check failed: tuple is referenced",
            function() s.index.name:delete{'Adam'} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key 'fkey1' integrity check failed: tuple is referenced",
            function() s.index.name:delete{'Eve'} end
        )
    end, {engine})
end
