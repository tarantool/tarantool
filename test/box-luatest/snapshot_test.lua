local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

-- https://bugs.launchpad.net/tarantool/+bug/695689: an error in box.snapshot()
-- is propagated to the caller.
g.test_snapshot_error_is_raised = function(cg)
    cg.server:exec(function()
        local errno = require('errno')
        local fio = require('fio')

        -- Test case prerequisite: some write operations should be executed
        -- after the initial database bootstrap, so the next box.snapshot()
        -- call has to write a new snapshot file.
        --
        -- luatest.server already performs a write to allow arbitrary code
        -- execution under the guest user. Ensure it here.
        t.assert_gt(box.info.signature, 0)

        -- One way to fail the snapshot creation is to occupy the target
        -- snapshot file name with a directory.
        --
        -- Another possible way is to remove write permissions on the directory
        -- with snapshots, but if tests are run from a superuser (it usually
        -- happens inside a docker container), the superuser bypasses all the
        -- filesystem permission check and the snapshot creation succeeds.
        local name = ('%020d.snap'):format(box.info.signature)
        fio.mkdir(fio.pathjoin(box.cfg.memtx_dir, name))

        -- Verify that the failure is reported.
        local ok, err = pcall(box.snapshot)
        t.assert_equals({ok, err.type, err.errno},
                        {false, 'SystemError', errno.EEXIST})
    end)
end

-- https://bugs.launchpad.net/tarantool/+bug/727174: SIGUSR1 writes a snapshot,
-- no crash.
g.test_snapshot_on_sigusr1 = function(cg)
    local signature = cg.server:exec(function() return box.info.signature end)

    -- Test case prerequisite (see test_snapshot_error_is_raised).
    t.assert_gt(signature, 0)

    local snap_name = ('%020d.snap'):format(signature)
    local snap_path = fio.pathjoin(cg.server.workdir, snap_name)
    t.assert_not(fio.path.exists(snap_path),
                 'the snapshot does not exist before the signal')

    -- SIGUSR1 should trigger snapshot writing.
    cg.server.process:kill('USR1')
    t.helpers.retrying({timeout = 60}, function()
        t.assert(fio.path.exists(snap_path), 'SIGUSR1 wrote the snapshot')
    end)

    -- Verify that the instance is alive.
    t.assert_equals(cg.server:exec(function() return box.info.status end),
                    'running', 'the server survived the signal')
end
