local t = require('luatest')

local g = t.group('gh-5104', {{engine = 'memtx'},
                              {engine = 'vinyl'}})

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

-- Check that index collation is inherited from the space format.
g.test_collation = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:format({{name = 'a', type = 'string', collation = 'unicode_ci'}})
        s:create_index('pk', {parts = {{'a', 'string'}}})

        s:insert{'ЕЛь'}
        t.assert_equals(s.index.pk.parts[1].collation, 'unicode_ci')
        t.assert_error_msg_contains('Duplicate key exists in unique index',
                                    s.insert, s, {'ель'})
    end, {engine})
end
