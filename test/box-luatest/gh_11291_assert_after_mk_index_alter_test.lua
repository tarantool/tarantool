local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Drop multikey index and create a non-multikey one.
g.test_drop_mk_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        for i = 1, 10 do
            s:replace{i, {i, i + 100, i + 1000}}
        end

        s:create_index('sk',
            {parts = {{field = 2, type = 'unsigned', path = '[*]'}}})

        for i = 11, 20 do
            s:replace{i, {i, i + 100, i + 1000}}
        end

        s.index.sk:drop()

        for i = 21, 30 do
            s:replace{i, {i, i + 100, i + 1000}}
        end

        s:create_index('sk',
            {parts = {{field = 2, type = 'unsigned', path = '[2]'}}})

        for i = 31, 40 do
            s:replace{i, {i, i + 100, i + 1000}}
        end
    end)
end

-- Turn multikey index into non-multikey one.
g.test_alter_mk_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        for i = 1, 10 do
            s:replace{i, {i, i + 100, i + 1000}}
        end

        local sk = s:create_index('sk',
            {parts = {{field = 2, type = 'unsigned', path = '[*]'}}})

        for i = 11, 20 do
            s:replace{i, {i, i + 100, i + 1000}}
        end

        sk:alter({parts = {{field = 2, type = 'unsigned', path = '[2]'}}})

        for i = 21, 30 do
            s:replace{i, {i, i + 100, i + 1000}}
        end
    end)
end
