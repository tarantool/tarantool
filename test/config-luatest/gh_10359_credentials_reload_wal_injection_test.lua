local t = require('luatest')
local treegen = require('luatest.treegen')
local yaml = require('yaml')
local server = require('luatest.server')

local g_reload = t.group('reload')

local errinj_role = [[
    return {
        validate = function(_cfg)
            -- No-op.
        end,
        apply = function(_cfg)
            box.error.injection.set('ERRINJ_WAL_IO', true)
        end,
        stop = function()
            box.error.injection.set('ERRINJ_WAL_IO', false)
        end,
    }
]]

local base_config = {
    credentials = {
        users = {
            guest = {
                roles = {'super'},
            },
            user_to_drop = {
                roles = {'super'},
                password = 'secret',
                privileges = {{
                    permissions = {'read'},
                    universe = true,
                }}
            },
            user_with_space_priv = {
                privileges = {{
                    permissions = {'read'},
                    spaces = {'_space'},
                }}
            },
            user_with_role_to_drop = {
                roles = {'role_granted_to_drop'},
            },
            myuser = {}
        },
        roles = {
            role_to_drop = {},
            role_granted_to_drop = {},
        }
    },
    roles = {'errinj'},
    iproto = {
        listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
    },
    groups = {
        ['group-001'] = {
            replicasets = {
                ['replicaset-001'] = {
                    instances = {
                        ['instance-001'] = {},
                    },
                },
            },
        },
    },
}

local function write_config(g, new_config)
    return treegen.write_file(g.cfg_dir, 'config.yaml',
                              yaml.encode(new_config))
end

g_reload.before_all(function(g)
    t.tarantool.skip_if_not_debug()

    g.cfg_dir = treegen.prepare_directory({}, {})
    treegen.write_file(g.cfg_dir, 'errinj.lua', errinj_role)
    local config_file = write_config(g, base_config)

    g.server = server:new({
        config_file = config_file,
        chdir = g.cfg_dir,
        alias = 'instance-001',
    })
    g.server:start()
end)

g_reload.after_all(function(g)
    if g.server ~= nil then
        g.server:drop()
        g.server = nil
    end
end)

local function commit_failure_msg(exp_operation)
    return 'credentials.apply: failed to commit credentials ' ..
        'transaction with pending operation(s): ' .. exp_operation ..
        ': Failed to write to disk'
end

local function reload_with_wal_io_error(g, config_for_reload, exp_operation)
    write_config(g, config_for_reload)

    local exp_err = commit_failure_msg(exp_operation)

    t.assert_error_msg_contains(exp_err, g.server.exec, g.server, function()
        require('config'):reload()
    end)

    g.server:exec(function(exp_err)
        local info = require('config'):info()
        t.assert_equals(info.status, 'check_errors')
        local found = false
        for _, alert in ipairs(info.alerts) do
            found = found or alert.message:find(exp_err, 1, true) ~= nil
        end
        t.assert(found)
    end, {exp_err})
end

g_reload.test_create_drop_user_error_context = function(g)
    -- Add a user that is absent from the applied base config.
    local config = table.deepcopy(base_config)
    config.credentials.users.user_to_create = {}

    reload_with_wal_io_error(g, config,
                             'box.schema.user.create("user_to_create")')

    -- Remove a config-managed user present in the applied base config.
    config = table.deepcopy(base_config)
    config.credentials.users.user_to_drop = nil

    reload_with_wal_io_error(g, config,
                             'box.schema.user.drop("user_to_drop")')
end

g_reload.test_create_drop_role_error_context = function(g)
    -- Add a role that is absent from the applied base config.
    local config = table.deepcopy(base_config)
    config.credentials.roles.role_to_create = {}

    reload_with_wal_io_error(g, config,
                             'box.schema.role.create("role_to_create")')

    -- Remove a config-managed role present in the applied base config.
    config = table.deepcopy(base_config)
    config.credentials.roles.role_to_drop = nil

    reload_with_wal_io_error(g, config,
                             'box.schema.role.drop("role_to_drop")')
end

g_reload.test_set_remove_password_error_context = function(g)
    -- Change an existing user's password.
    local config = table.deepcopy(base_config)
    config.credentials.users.user_to_drop.password = 'new_pass'

    reload_with_wal_io_error(g, config,
                             'box.schema.user.passwd("user_to_drop")')

    -- Remove an existing user's password.
    config = table.deepcopy(base_config)
    config.credentials.users.user_to_drop.password = nil

    reload_with_wal_io_error(g, config,
                             'box.schema.user.passwd("user_to_drop", nil)')
end

g_reload.test_grant_revoke_role_error_context = function(g)
    -- Grant a configured role to an existing user.
    local config = table.deepcopy(base_config)
    config.credentials.users.myuser = {
        roles = {'role_to_drop'},
    }

    reload_with_wal_io_error(g, config,
        'box.schema.user.grant("myuser", "execute", "role", "role_to_drop")')

    -- Revoke a configured role from an existing user.
    config = table.deepcopy(base_config)
    config.credentials.users.user_to_drop.roles = {}

    reload_with_wal_io_error(g, config,
        'box.schema.user.revoke("user_to_drop", "execute", "role", "super")')
end

g_reload.test_grant_revoke_privilege_error_context = function(g)
    -- Grant a universe privilege to an existing user.
    local config = table.deepcopy(base_config)
    config.credentials.users.myuser = {
        privileges = {{
            permissions = {'read'},
            universe = true,
        }},
    }

    reload_with_wal_io_error(g, config,
        'box.schema.user.grant("myuser", "read", "universe", "")')

    -- Revoke a universe privilege from an existing user.
    config = table.deepcopy(base_config)
    config.credentials.users.user_to_drop.privileges = nil

    reload_with_wal_io_error(g, config,
        'box.schema.user.revoke("user_to_drop", "read", "universe", "")')
end

g_reload.test_revoke_space_privilege_error_context = function(g)
    local config = table.deepcopy(base_config)
    config.credentials.users.user_with_space_priv.privileges = nil

    reload_with_wal_io_error(g, config,
        'box.schema.user.revoke("user_with_space_priv", "read", "space", ' ..
        '"_space")')
end

g_reload.test_drop_role_revoke_role_grant_error_context = function(g)
    local config = table.deepcopy(base_config)
    config.credentials.users.user_with_role_to_drop.roles = {}
    config.credentials.roles.role_granted_to_drop = nil

    reload_with_wal_io_error(g, config,
        'box.schema.role.drop("role_granted_to_drop")')
end

-- Async per-object sync: late object creation applies a pending grant in the
-- background; WAL failures are logged (not raised or alerted).
local g_delayed = t.group('delayed')

g_delayed.before_all(function(g)
    t.tarantool.skip_if_not_debug()

    g.cfg_dir = treegen.prepare_directory({}, {})

    local config = table.deepcopy(base_config)
    config.roles = nil
    config.credentials.users.myuser = {
        privileges = {{
            permissions = {'read'},
            spaces = {'late_space'},
        }, {
            permissions = {'execute'},
            functions = {'late_func'},
        }},
    }
    local config_file = write_config(g, config)

    g.server = server:new({
        config_file = config_file,
        chdir = g.cfg_dir,
        alias = 'instance-001',
    })
    g.server:start()
end)

g_delayed.after_all(function(g)
    if g.server ~= nil then
        g.server:drop()
        g.server = nil
    end
end)

g_delayed.after_each(function(g)
    g.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_IO', false)
    end)
end)

-- Create the object and enable the WAL error injection on its commit, so that
-- the following pending grant transaction fails to write to disk.
local function create_object_with_wal_io_error(g, create_fn, exp_operation)
    g.server:exec(function(create_fn)
        box.begin()
        box.on_commit(function()
            box.error.injection.set('ERRINJ_WAL_IO', true)
        end)
        loadstring(create_fn)()
        box.commit()
    end, {create_fn})

    local exp_err = commit_failure_msg(exp_operation)

    local exp_pattern = exp_err:gsub('[%(%)%.%%%+%-%*%?%[%]%^%$]', '%%%1')

    t.helpers.retrying({timeout = 5}, function()
        t.assert(g.server:grep_log(exp_pattern),
                 'Expected error in log: ' .. exp_err)
    end)
end

g_delayed.test_grant_space_privilege_on_create_error_context = function(g)
    create_object_with_wal_io_error(g,
        'box.schema.space.create("late_space")',
        'box.schema.user.grant("myuser", "read", "space", "late_space")')
end

g_delayed.test_grant_function_privilege_on_create_error_context = function(g)
    create_object_with_wal_io_error(g,
        'box.schema.func.create("late_func")',
        'box.schema.user.grant("myuser", "execute", "function", "late_func")')
end
