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
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        if box.space.renamed ~= nil then
            box.space.renamed:drop()
        end
    end)
end)

-- Checks that the space object of a dropped and rolled back space
-- is kept in sync with the space from the box.space namespace.
g.test_drop_rollback = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')

        -- Drop the space and roll it back.
        box.begin()
        box.space.test:drop()
        box.rollback()

        -- The local variable and the entry from the box.space
        -- namespace reference the same object.
        t.assert(s == box.space.test)
    end)
end

-- The same check but with data inserts and rename involved.
g.test_rename_drop_rollback = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')

        s:create_index('pk')
        s:insert({1, 1})

        -- Rename and drop the space and roll it back.
        -- Do some inserts in the way.
        box.begin()
        box.space.test:insert({2, 2})
        box.space.test:rename('renamed')
        box.space.renamed:insert({3, 3})
        box.space.renamed:drop()
        box.rollback()

        -- The local variable and the entry from the box.space
        -- namespace reference the same object.
        t.assert(s == box.space.test)

        -- The space still has the same name.
        t.assert_equals(s.name, 'test')

        -- Contents hadn't changed.
        t.assert_equals(s:select(), {{1, 1}})
    end)
end

-- The same check but with more complex transaction with an attempt to
-- trick a system by creating another space with the same ID and name.
g.test_sophisticated_transaction = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {format = {'id'}})

        s:create_index('pk')
        s:insert({1, 1})

        -- More sophisticated transaction: drop the old space, create a new one
        -- with the same name but with a different format, roll everything back.
        box.begin()
        box.space.test:insert({2, 2})
        box.space.test:drop()
        box.schema.space.create('test', {format = {'identifier'}})
        box.space.test:create_index('pk')
        box.space.test:insert({3, 3})
        box.rollback()

        -- The local variable and the entry from the
        -- box.space namespace are the same objects.
        t.assert(s == box.space.test)

        -- The space format and contents still the same.
        t.assert_equals(s:format(), {{name = 'id', type = 'any'}})
        t.assert_equals(s:select(), {{1, 1}})
    end)
end
