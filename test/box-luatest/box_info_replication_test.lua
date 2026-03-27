-- tags: parallel
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local dump_repl_info = false

local g = t.group('box_info_repl_write_secret', {
    {
        ERRINJ_BOX_INFO_REPL_WRITE_SECRET = false,
    },
    {
        ERRINJ_BOX_INFO_REPL_WRITE_SECRET = true,
    },
})

g.before_all(function()
    local config = cbuilder:new()
        :use_group('g-001')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :set_global_option('credentials.users.replicator.password', '123')
        :config()
    g.cluster = cluster:new(config)
    g.cluster:start()
end)

g.after_all(function()
    g.cluster:drop()
    g.cluster = nil
end)

g.test = function(cg)
    g.cluster['i-001']:exec(function(enabled, dump_info)
        local tnt = require('tarantool')
        local log = require('log')
        local yaml = require('yaml')
        local uri = require('uri')

        local ndebug = string.find(tnt.build.flags, '-DNDEBUG') ~= nil
        box.error.injection.set('ERRINJ_BOX_INFO_REPL_WRITE_SECRET', enabled)

        local repl_info = box.info.replication
        if dump_info then
            log.info('box.replication.info:\n%s', yaml.encode(repl_info))
        end
        -- Replication id = 2 of second server is expected.
        local info = repl_info[2]
        t.assert(info.name == 'i-002')
        -- Check replication is established.
        t.assert(info.upstream ~= nil)
        t.assert(info.upstream.peer ~= nil)
        -- Check error injection.
        local parsed_peer = uri.parse(info.upstream.peer)
        t.assert(parsed_peer ~= nil)
        if not ndebug and enabled then
            t.assert_equals(parsed_peer.password, '123')
        else
            t.assert_equals(parsed_peer.password, nil)
        end
    end, {cg.params.ERRINJ_BOX_INFO_REPL_WRITE_SECRET, dump_repl_info})
end
