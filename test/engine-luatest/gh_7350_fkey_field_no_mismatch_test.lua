local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-7350-fkey-field-no-mismatch',
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

g.test_fkey_field_no = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local s1 = box.schema.create_space('s1', {engine=engine})

        local fmt = {{name='f1', type='any', foreign_key={k1={space=s1.id, field=2}}},
                     {name='f2', type='any', foreign_key={k2={space=s1.id, field='f3'}}},
                     {name='f3', type='any', foreign_key={k3={space=s1.id, field=4}}}}
        local fkey = {k4={space=s1.id, field={[5]=7, ['f6']='f5', [4]=6}}}

        local opts = {engine=engine, format=fmt, foreign_key=fkey}
        local s2 = box.schema.create_space('s2', opts)

        t.assert_equals(s2:format(), fmt)
        t.assert_equals(s2.foreign_key, fkey)
        t.assert_equals(s2:format(s2:format()), nil)
    end, {engine})
end
