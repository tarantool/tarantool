local cluster = require('luatest.replica_set')
local t = require('luatest')

local g = t.group('gh-6310-grant-rw-access-on-_session_settings-space-to-public-role')

g.before_all(function()
    g.cluster = cluster:new({})
    g.bootstrap = g.cluster:build_and_add_server({alias = 'bootstrap'})

    local data_dir = 'test/box-luatest/upgrade/2.9.1'
    g.upgrade = g.cluster:build_and_add_server({alias   = 'upgrade',
                                                datadir = data_dir})

    g.cluster:start()
    g.upgrade:exec(function()
        box.schema.upgrade()
    end)
end)

g.after_all(function()
    g.cluster:drop()
end)

g.test_boostrap = function()
    g.bootstrap:exec(function()
        local _session_settings_privs
        local public_privs = box.schema.role.info('public')
        for _, priv in pairs(public_privs) do
            if priv[3] == '_session_settings' then
                _session_settings_privs = priv[1]
            end
        end
        local msg = 'public role has read,write access on ' ..
                    '_session_settings space on bootstrapped instance'
        t.assert(_session_settings_privs and
                 _session_settings_privs:find('read,write'), msg)

        box.schema.user.create('test')
        box.session.su('test')
        local _session_settings = box.space._session_settings
        msg = 'newly created user has read access on _session_settings space'
        t.assert(pcall(_session_settings.select, _session_settings), msg)
        msg = 'newly created user has write access on _session_settings space'
        t.assert(pcall(_session_settings.update, _session_settings,
                       'sql_default_engine', {{'=', 2, 'vinyl'}}), msg)
    end)
end

g.test_upgrade = function()
    g.upgrade:exec(function()
        local _session_settings_privs
        local public_privs = box.schema.role.info('public')
        for _, priv in pairs(public_privs) do
            if priv[3] == '_session_settings' then
                _session_settings_privs = priv[1]
            end
        end
        local msg = 'public role has read,write access on ' ..
                '_session_settings space on upgraded instance'
        t.assert(_session_settings_privs and
                 _session_settings_privs:find('read,write'), msg)

        box.schema.user.create('test')
        box.session.su('test')
        local _session_settings = box.space._session_settings
        msg = 'newly created user has read access on _session_settings space'
        t.assert(pcall(_session_settings.select, _session_settings), msg)
        msg = 'newly created user has write access on _session_settings space'
        t.assert(pcall(_session_settings.update, _session_settings,
                       'sql_default_engine', {{'=', 2, 'vinyl'}}), msg)
    end)
end
