local server = require('luatest.server')
local t = require('luatest')

local g = t.group("keys", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Make sure that 'pragma table_info()' correctly handles tables
-- without primary key.
g.test_gh_4745_table_info_assertion = function(cg)
    cg.server:exec(function(engine)
        local T = box.schema.create_space('T', {
            engine = engine,
            format = {{'i', 'integer'}}
        })
        local _, err = box.execute([[pragma table_info(T);]])
        t.assert_equals(err, nil)
        T:drop()
        t.assert_equals(box.space.T, nil)
    end, {cg.params.engine})
end
