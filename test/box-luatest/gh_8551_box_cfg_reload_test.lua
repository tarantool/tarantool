local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.master = server:new({alias = 'gh_8551_master'})
    cg.master:start()
    cg.master:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('primary')
    end)
    cg.replica = server:new({
        alias = 'gh_8551_replica',
        box_cfg = {
            replication = server.build_listen_uri('gh_8551_master')
        },
    })
    cg.replica:start()
end)

g.after_all(function(cg)
    cg.master:drop()
end)

g.test_listen = function(cg)
    cg.master:exec(function()
        local fio = require('fio')
        local old_listen = box.cfg.listen
        local sock_path = fio.pathjoin(fio.cwd(), 'gh_8551_test.sock')
        t.assert_not(fio.path.exists(sock_path))
        box.cfg({listen = sock_path})
        -- Delete the socket file and reconfigure box.cfg.listen.
        -- The socket file must be recreated.
        t.assert(fio.path.exists(sock_path))
        t.assert(fio.unlink(sock_path))
        t.assert_not(fio.path.exists(sock_path))
        box.cfg({listen = sock_path})
        t.assert(fio.path.exists(sock_path))
        t.assert(fio.unlink(sock_path))
        t.assert_not(fio.path.exists(sock_path))
        -- Reset to the default box.cfg.listen.
        box.cfg({listen = old_listen})
        t.assert_not(fio.path.exists(sock_path))
    end)
end

g.test_replication = function(cg)
    cg.replica:exec(function()
        -- Insert a conflicting record.
        box.space.test:insert({1})
    end)
    cg.master:exec(function()
        box.space.test:insert({1})
    end)
    cg.replica:exec(function()
        -- Wait for replication to stop on conflict.
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1])
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status, 'stopped')
        end)
        -- Delete the conflicting record and restart replication.
        box.space.test:delete({1})
        box.cfg({replication = box.cfg.replication})
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1])
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
end
