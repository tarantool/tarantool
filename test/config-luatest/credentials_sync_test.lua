local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local fio = require('fio')

local g = t.group(
    "credentials_sync",
    t.helpers.matrix({
        restart = {
            function(c, config)
                -- Instance restart
                --
                -- We must call sync method before stop/start.
                -- Otherwise, the new configuration won't be written to disk and
                -- the instance will restart with the old one.
                c:sync(config)
                c:stop()
                c:start()
            end,
            function(c, config)
                -- Config reload
                c:reload(config)
            end,
        }
    })
)

local default_user_config = cbuilder:new()
    :add_instance('i-001', {})
    :set_global_option('credentials.users.alice')
    :set_global_option('credentials.users.alice.password', 'ALICE')
    :set_global_option('credentials.users.charlie')
    :set_global_option('credentials.users.charlie.password', 'CHARLIE')
    :config()

local default_role_config = cbuilder:new()
    :add_instance('i-001', {})
    :set_global_option('credentials.roles.reader', {
        privileges = {
            {permissions = {'read'}, universe = true},
        },
    })
    :set_global_option('credentials.roles.writer', {
        privileges = {
            {permissions = {'write'}, universe = true},
        },
    })
    :config()

local function user_exists(inst, name)
    return inst:exec(function(n)
        return box.schema.user.exists(n)
    end, {name})
end

local function role_exists(inst, name)
    return inst:exec(function(n)
        return box.schema.role.exists(n)
    end, {name})
end

local function get_perms(inst, who)
    return inst:exec(function(name)
        local internal =
            require('internal.config.applier.credentials')._internal
        return internal.privileges_from_box(name)
    end, {who})
end

g.test_config_user_removed = function(g)
    local c = cluster:new(default_user_config)
    c:start()

    c['i-001']:exec(function()
        box.schema.user.create('bob')
    end)

    t.assert(user_exists(c['i-001'], 'alice'))
    t.assert(user_exists(c['i-001'], 'charlie'))
    t.assert(user_exists(c['i-001'], 'bob'))

    local new_config = cbuilder:new(default_user_config)
        :set_global_option('credentials.users.alice', nil)
        :config()

    g.params.restart(c, new_config)

    -- User removed from config must be dropped.
    t.assert_not(user_exists(c['i-001'], 'alice'))

    -- Users still in config must remain.
    t.assert(user_exists(c['i-001'], 'charlie'))

    -- Manually created user must remain.
    t.assert(user_exists(c['i-001'], 'bob'))
end

g.test_config_role_removed = function(g)
    local c = cluster:new(default_role_config)
    c:start()

    c['i-001']:exec(function()
        if not box.schema.role.exists('auditor') then
            box.schema.role.create('auditor')
        end
    end)

    t.assert(role_exists(c['i-001'], 'reader'))
    t.assert(role_exists(c['i-001'], 'writer'))
    t.assert(role_exists(c['i-001'], 'auditor'))

    local new_config = cbuilder:new(default_role_config)
        :set_global_option('credentials.roles.reader', nil)
        :config()

    g.params.restart(c, new_config)

    -- Role removed from config must be dropped.
    t.assert_not(role_exists(c['i-001'], 'reader'))

    -- Roles still in config must remain.
    t.assert(role_exists(c['i-001'], 'writer'))

    -- Manually created role must remain.
    t.assert(role_exists(c['i-001'], 'auditor'))
end

g.test_manual_then_config_then_removed_from_config = function(g)
    -- Start without dualuser/dualrole in the config.
    local base_cfg = cbuilder:new()
        :add_instance('i-001', {})
        :set_global_option('credentials.users.guest', {roles = {'super'}})
        :config()

    local c = cluster:new(base_cfg)
    c:start()

    -- Create user and role manually.
    c['i-001']:exec(function()
        if not box.schema.user.exists('dualuser') then
            box.schema.user.create('dualuser')
        end
        if not box.schema.role.exists('dualrole') then
            box.schema.role.create('dualrole')
        end
    end)

    t.assert(user_exists(c['i-001'], 'dualuser'))
    t.assert(role_exists(c['i-001'],  'dualrole'))

    -- At this point there must be no config-origin privileges yet.
    local perms_user  = get_perms(c['i-001'], 'dualuser')
    local perms_role  = get_perms(c['i-001'], 'dualrole')
    t.assert_equals(((perms_user.universe or {})[''] or {}).execute, nil)
    t.assert_equals(((perms_role.universe or {})[''] or {}).read, nil)

    -- Add dualuser/dualrole to the config with privileges.
    local new_config = cbuilder:new(base_cfg)
        :set_global_option('credentials.users.dualuser', {
            privileges = {{permissions = {'execute'}, universe = true}},
        })
        :set_global_option('credentials.roles.dualrole', {
            privileges = {{permissions = {'read'}, universe = true}},
        })
        :config()

    g.params.restart(c, new_config)

    -- After reload: objects still exist (they were manual),
    -- and config-origin privileges are added.
    t.assert(user_exists(c['i-001'], 'dualuser'))
    t.assert(role_exists(c['i-001'],  'dualrole'))

    perms_user = get_perms(c['i-001'], 'dualuser')
    perms_role = get_perms(c['i-001'], 'dualrole')
    t.assert_equals(((perms_user.universe or {})[''] or {}).execute, true)
    t.assert_equals(((perms_role.universe or {})[''] or {}).read, true)

    -- Remove them from the config and reload again.
    new_config = cbuilder:new(new_config)
        :set_global_option('credentials.users.dualuser', nil)
        :set_global_option('credentials.roles.dualrole', nil)
        :config()

    g.params.restart(c, new_config)

    -- dualuser/dualrole remain (manual origin still exists),
    -- BUT config-origin privileges are revoked.
    t.assert(user_exists(c['i-001'], 'dualuser'))
    t.assert(role_exists(c['i-001'],  'dualrole'))

    perms_user = get_perms(c['i-001'], 'dualuser')
    perms_role = get_perms(c['i-001'], 'dualrole')

    -- execute from YAML must be revoked
    t.assert_equals(((perms_user.universe or {})[''] or {}).execute, nil)

    -- read from YAML must be revoked
    t.assert_equals(((perms_role.universe or {})[''] or {}).read, nil)
end

local g2 = t.group("find-orphan-users")

g2.test_find_orphan_users_script = function()
    local config = cbuilder:new(default_user_config)
        :set_global_option('credentials.roles.reader', {})
        :set_global_option('credentials.roles.writer', {})
        :config()

    local c = cluster:new(config)
    c:start()

    -- Create user and role manually.
    c['i-001']:exec(function()
        box.schema.user.create('alice')  -- duplicates config user
        box.schema.user.create('bob')    -- orphan user
        box.schema.role.create('reader') -- duplicates config role
        box.schema.role.create('tester') -- orphan role
    end)

    c:reload(config)

    local find_orphan_users_script =
        fio.abspath('tools/find-orphan-users.lua')
    t.assert(fio.path.exists(find_orphan_users_script))

    c['i-001']:exec(function(find_orphan_users_script)
        local info = require('config'):info()
        local exp_alert_sub =
            'Found users/roles authored from Lua and not managed by'
        t.assert_equals(info.status, 'check_warnings', info)
        t.assert_equals(#info.alerts, 1, info.alerts)
        t.assert_equals(info.alerts[1].type, 'warn')
        t.assert_str_contains(tostring(info.alerts[1].message), exp_alert_sub)

        -- Run the helper. It returns a table of lines.
        local lines = dofile(find_orphan_users_script)
        t.assert_equals(type(lines), 'table')

        -- Helper to check that a line exists (order-independent).
        local function has_line(needle)
            for _, s in ipairs(lines) do
                if s == needle then
                    return true
                end
            end
            return false
        end

        -- Validate a few key lines from the sample.
        -- Entries listed as managed by YAML:
        t.assert(has_line('role "reader"'))
        t.assert(has_line('role "writer"'))
        t.assert(has_line('user "replicator"')) -- from base_config in cbuilder
        t.assert(has_line('user "client"'))     -- from base_config in cbuilder
        t.assert(has_line('user "alice"'))
        t.assert(has_line('user "charlie"'))

        -- Commands to transfer ownership to YAML (disown duplicates):
        t.assert(has_line('box.schema.role.disown("reader")'))
        t.assert(has_line('box.schema.user.disown("alice")'))

        -- Commands to drop orphans:
        t.assert(has_line('box.schema.user.drop("bob")'))
        t.assert(has_line('box.schema.role.drop("tester")'))

        -- Execute all generated commands (both disown and drop).
        -- We find all lines that look like box.schema.*.<op>(...)
        for _, s in ipairs(lines) do
            if s:match('^box%.schema%.[%w_]+%.[%w_]+%(') then
                local fn, err = load(s)
                t.assert(fn, ('failed to compile: %s'):format(err or s))
                fn()
            end
        end
    end, {find_orphan_users_script})

    -- Reload with the same config: the orphan/duplicate alerts should be gone.
    c:reload(config)

    c['i-001']:exec(function()
        local info = require('config'):info()
        t.assert_equals(info.status, 'ready', info)
        t.assert_equals(#info.alerts, 0, info.alerts)
    end)
end
