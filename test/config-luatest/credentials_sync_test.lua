local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

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
