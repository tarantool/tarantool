local t = require('luatest')
local server = require('luatest.server')

local g = t.group('replication_synchro_timeout_does_not_roll_back')
--
-- gh-7486: `replication_synchro_timeout` does not roll back.
--
local wait_timeout = 10

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_replication_synchro_timeout_rolls_back_if_old = function(cg)
    cg.server:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.01,
        }
        require('compat').replication_synchro_timeout = 'old'
        t.assert_error_msg_content_equals(
            'Quorum collection for a synchronous transaction is timed out',
            box.space.test.insert, box.space.test, {1})
        box.cfg{ replication_synchro_quorum = 2, }
    end)
end

-- We can't make sure that replication_synchro_timeout will never roll back the
-- transaction, but we can check that it won't happen in some period longer
-- than replication_synchro_timeout by 10 times, for example.
g.test_replication_synchro_timeout_does_not_roll_back_if_new = function(cg)
    cg.server:exec(function(wait_timeout)
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.01,
        }
        local fiber = require('fiber')
        require('compat').replication_synchro_timeout = 'new'
        local f = fiber.create(function() box.space.test:insert{1} end)
        f:set_joinable(true)
        t.helpers.retrying({timeout = wait_timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
        fiber.sleep(0.1)
        box.cfg{ replication_synchro_quorum = 1, }
        local ok, _ = f:join()
        t.assert(ok)
        box.space.test:drop()
    end, {wait_timeout})
end
