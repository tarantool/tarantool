local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{alias = 'master'}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_wakeup_writing_to_wal_fiber = function()
    g.server:exec(function()
        local fiber = require('fiber')

        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/box-luatest/?.so;'..build_path..'/test/box-luatest/?.dylib;'..package.cpath
        local lib = box.lib.load('gh_6506_lib')
        local save_fiber = lib:load('save_fiber')
        local wakeup_saved = lib:load('wakeup_saved')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        local f = fiber.new(function()
            save_fiber()
            s:replace{1}
            fiber.yield()
        end)
        f:set_joinable(true)

        -- Start fiber f
        fiber.yield()
        -- Wakeup f while it is writing to WAL
        wakeup_saved()
        -- Check that f did not crash
        local st, _ = f:join()
        t.assert(st)
    end)
end
