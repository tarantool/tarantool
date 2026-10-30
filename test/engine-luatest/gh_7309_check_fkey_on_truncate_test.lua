local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-7309-check-fkey-on-truncate',
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
        if box.space.s2 then box.space.s2:drop() end
        if box.space.s1 then box.space.s1:drop() end
    end)
end)

g.test_fkey_truncate = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fk = {asd = {field = 'a', space = 's1'}}
        local o1 = {engine = engine, format = {{'a', 'integer'}}}
        local o2 = {engine = engine, format = {{'a', 'integer'},
                                               {'b', 'integer',
                                                foreign_key = fk}}}
        local s1 = box.schema.create_space('s1', o1)
        local s2 = box.schema.create_space('s2', o2)

        -- Try to drop a space *without* a primary key index
        t.assert_error_msg_content_equals(
            "Can't modify space 's1': space is referenced by foreign key",
            function() s1:drop() end)

        s1:create_index('pk')
        s2:create_index('pk')
        s1:insert({1, 1})
        s2:insert({2, 1})

        -- Try to drop a space *with* a primary key index
        t.assert_error_msg_content_equals(
            "Can't modify space 's1': space is referenced by foreign key",
            function() s1:drop() end)

        t.assert_error_msg_content_equals(
            "Can't modify space 's1': space is referenced by foreign key",
            function() s1:truncate() end)

        s2:insert({3, 1})
    end, {engine})
end
