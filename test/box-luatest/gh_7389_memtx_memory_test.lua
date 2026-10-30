local server = require('luatest.server')
local t = require('luatest')
local g = t.group('memtx_memory setting test')

g.before_all(function(g)
    g.server = server:new{
        alias = 'default',
        box_cfg = {memtx_memory = 0}
    }
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

-- Any value we specify in memtx_memory which is
-- lesser than 64MB is rounded up to 64MB.
g.test_memtx_memory_setting = function(g)
    g.server:exec(function()
        local MB = 1024 * 1024
        local err = "Incorrect value for option 'memtx_memory':"
                    .." cannot decrease memory size at runtime"

        -- 0 rounds-up to 64M, so the initial memtx_memory is 64M.
        -- See the box_cfg parameter of server:new.
        t.assert_equals(box.slab.info().quota_size, 64 * MB)

        -- Setting memtx_memory to 0 again should succeed.
        box.cfg({memtx_memory = 0})
        t.assert_equals(box.slab.info().quota_size, 64 * MB)

        -- Setting it from 64MB to 64MB does nothing.
        box.cfg({memtx_memory = 64 * MB})
        t.assert_equals(box.slab.info().quota_size, 64 * MB)

        -- Setting it less than 64M again does nothing,
        -- because it rounds up to 64M again.
        box.cfg({memtx_memory = 32 * MB})
        t.assert_equals(box.slab.info().quota_size, 64 * MB)

        -- Now let's increase the memtx_memory.
        box.cfg({memtx_memory = 128 * MB})
        t.assert_equals(box.slab.info().quota_size, 128 * MB)

        -- Attempt to set it back to 64M will fail.
        t.assert_error_msg_equals(err, box.cfg, {memtx_memory = 64 * MB})
        t.assert_equals(box.slab.info().quota_size, 128 * MB)

        -- Attempt to set it to a value lesser than 64M will fail too,
        -- because it rounds up the value to 64M and in fact it is the
        -- same as setting it to 64M.
        t.assert_error_msg_equals(err, box.cfg, {memtx_memory = 32 * MB})
        t.assert_equals(box.slab.info().quota_size, 128 * MB)

        -- Do the same with non-power-of-two, just in case.
        box.cfg({memtx_memory = 200 * MB})
        t.assert_equals(box.slab.info().quota_size, 200 * MB)

        t.assert_error_msg_equals(err, box.cfg, {memtx_memory = 100 * MB})
        t.assert_equals(box.slab.info().quota_size, 200 * MB)
    end)
end
