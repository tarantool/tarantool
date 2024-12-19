local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.before_each(function(g)
    g.server:exec(function()
        box.schema.sequence.create('id_seq', { if_not_exists = true })
        local s = box.schema.create_space('test', {
            format = {
                {'id', type='unsigned', is_nullable=false},
                {'name', type='string', is_nullable=false},
            },
            if_not_exists = true,
        })
        s:create_index('primary', {
            sequence = 'id_seq',
            if_not_exists = true
        })
        s:insert{nil, 'aaa'}
        s:insert{nil, 'bbb'}
        s:insert{nil, 'ccc'}
        local seq = box.sequence.id_seq
        seq:reset()
        s:delete(1)
    end)
end)

local common = function()
    local s = box.space.test
    s:insert{nil, 'ddd'}
    local ddd = s:get(1)
    t.assert_equals(ddd[2], 'ddd')
end

g.test_no_recovery = function(g)
    g.server:exec(common)
end

g.test_snapshot_recovery = function(g)
    g.server:exec(function()
        box.snapshot()
        local fiber = require('fiber')
        fiber.sleep(0.1)
    end)
    g.server:restart()
    g.server:exec(common)
end

g.test_wal_recovery = function(g)
    g.server:restart()
    g.server:exec(common)
end

g.after_each(function(g)
    g.server:exec(function()
        box.space.test:drop()
    end)
end)


g.after_all(function(g)
    g.server:drop()
end)
