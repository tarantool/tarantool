local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = cg.master.net_box_uri,
            replication_connect_quorum = 0,
        },
    })
    cg.replica:start()
end)

g.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

g.test_iproto_id_error = function(cg)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_DISABLE_ID', true)
    end)
    cg.replica:exec(function(uri)
        local t = require('luatest')
        t.assert_is_not(box.info.replication[1].upstream, nil)
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {}})
        t.assert_is(box.info.replication[1].upstream, nil)
        box.cfg({replication = uri})
        t.helpers.retrying({}, function()
            t.assert_is_not(box.info.replication[1].upstream, nil)
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end, {g.master.net_box_uri})
    t.assert(g.replica:grep_log(
        'ER_UNKNOWN_REQUEST_TYPE: Unknown request type 73'))
    t.assert(g.replica:grep_log('IPROTO_ID failed'))
end
