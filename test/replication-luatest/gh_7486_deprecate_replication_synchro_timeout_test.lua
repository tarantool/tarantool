local t = require('luatest')
local server = require('luatest.server')

local g = t.group('deprecate_replication_synchro_timeout')
--
-- gh-7486: deprecate `replication_synchro_timeout`.
--
local wait_timeout = 10

g.before_all(function(cg)
    cg.server = server:new()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- We cannot check that the timeout is truly infinite,
-- but we can check that it is not 0.
g.test_zero_value_does_not_rollback_immediately = function(cg)
    cg.server:exec(function(wait_timeout)
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0,
        }
        box.ctl.promote()
        box.ctl.wait_rw()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
        local fiber = require('fiber')
        local f = fiber.create(function() box.space.test:insert{1} end)
        t.helpers.retrying({timeout = wait_timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
        fiber.sleep(1)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.cfg{ replication_synchro_quorum = 1, }
        t.helpers.retrying({timeout = wait_timeout},
            function() t.assert_equals(f:status(), 'dead') end)
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.space.test:drop()
    end, {wait_timeout})
end
