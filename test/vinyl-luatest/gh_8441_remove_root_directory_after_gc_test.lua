local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        -- Make each snapshot trigger garbage collection.
        box.cfg{checkpoint_count = 1}
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
            box.snapshot()
        end
    end)
end)

g.test_root_directory_is_removed_after_gc = function(cg)
    cg.server:exec(function()
        local fio = require('fio')

        -- Create vinyl space.
        box.schema.space.create("test", {
            format = {
                { "id", "unsigned", is_nullable = false },
                { "a", "unsigned", is_nullable = false }
            },
            engine = "vinyl",
        })
        box.space.test:create_index("id", {
            type = "TREE",
            unique = true,
            parts = { { field = "id", type = "unsigned" } },
        })
        box.space.test:create_index("a", {
            type = "TREE",
            unique = false,
            parts = { { field = "a", type = "unsigned" } },
        })

        local space_id = box.space.test.id

        -- LSM directories are created on demand.
        t.assert_not(fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id)))

        -- Populate runs with data.
        box.space.test:insert({1, 1})

        -- Flush runs to disk.
        box.snapshot()

        t.assert(fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id)))

        box.space.test:drop()

        -- Remove run files and LSM root.
        box.snapshot()

        -- Check that all LSM directories are removed.
        t.assert_not(fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id)))
    end)
end

g.test_recreate_space_with_old_id = function(cg)
    cg.server:exec(function()
        local fio = require('fio')

        -- Create vinyl space.
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('pk')

        local space_id = box.space.test.id

        -- LSM directories are created on demand.
        t.assert_not(fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id)))

        -- Populate runs with data.
        box.space.test:insert({1})

        -- Flush runs to disk.
        box.snapshot()

        box.space.test:drop()

        -- Check that all LSM directories with our space id still exist.
        t.assert(fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id)))

        -- Recreate vinyl space with the same id.
        box.schema.create_space('test', {engine = 'vinyl', id = space_id})
        box.space.test:create_index('pk')

        -- Remove the run files of the previous version of the space
        -- and the LSM root which is empty now (though the new version
        -- of the space still exists).
        box.snapshot()

        -- Check that all LSM directories are removed.
        t.assert_equals(
            fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id, 0)),
            false
        )
        t.assert_not(fio.path.exists(fio.pathjoin(box.cfg.vinyl_dir, space_id)))
    end)
end
