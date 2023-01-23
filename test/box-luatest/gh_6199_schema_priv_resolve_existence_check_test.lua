local cluster = require('luatest.replica_set')
local t = require('luatest')

local g = t.group('gh-6199-schema-priv-resolve-existence-check')

g.before_all(function()
    g.cluster = cluster:new({})
    g.default = g.cluster:build_and_add_server({alias = 'default'})

    g.cluster:start()
    g.default:exec(function()
        box.session.su('admin')
    end)
end)

g.after_all(function()
    g.cluster:drop()
end)

g.test_priv_resolve_existence_check = function()
    g.default:exec(function()
        local msg = 'role cannot be granted together with a privilege'
        t.assert_error(box.schema.user.grant, 'guest', 'read,replication',
                       'universe', msg)

        msg = 'when privileges are resolved, we check that all of them have' ..
              'been resolved'
        local privs = {'read', 'write', 'execute', 'session', 'usage', 'read',
                       'create', 'drop', 'alter', 'reference', 'trigger',
                       'update', 'delete'}
        for _, priv in pairs(privs) do
            local invalid_priv = ('%s, unknown,%s'):format(priv, priv)
            t.assert_error(box.schema.user.grant, 'guest', invalid_priv,
                           'universe', msg)
        end
        local invalid_all_privs = table.concat(privs, ',') .. ',unknown' ..
                                  table.concat(privs, ',')
        t.assert_error(box.schema.user.grant, 'guest', invalid_all_privs,
                       'universe', msg)
    end)
end
