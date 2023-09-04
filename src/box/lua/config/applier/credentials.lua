local log = require('internal.config.utils.log')
local digest = require('digest')

--[[
This intermediate representation is a formatted set of
all permissions for a user or role. It is required to
standardize diff functions. All the validation is done
by config or box.info(), so neither the format nor the
helper function don't perform it. Below you can find two
converters to this representation, from box format and
config schema format, accordingly `privileges_{box,config}_convert()`.

[obj_type][obj_name] = {
    read = true,
    write = true,
    ...
}

obj_types:
 - 'user'
 - 'role'
 - 'space'
 - 'function'
 - 'sequence'
 - 'universe'

obj_names:
 - mostly user defined strings, provided by config or box
 - special value '', when there is no obj_name, e.g. for
   'universe' obj_type or for granting permission for all
   objects of a type.

privs:
 - read
 - write
 - execute
   - lua_eval
   - lua_call
   - sql
 - session
 - usage
 - create
 - drop
 - alter
 - reference
 - trigger
 - insert
 - update
 - delete

Examples:
- - box.schema.user.grant('myuser', 'execute', 'function', 'myfunc')
  - ['function']['myfunc']['execute'] = true
  - grant execute of myfunc

- - box.schema.user.grant('myuser', 'execute', 'function')
  - ['function']['']['execute'] = true
  - grant execute of all registered functions

- - box.schema.user.grant('myuser', 'read', 'universe')
  - ['universe']['']['read'] = true
  - grant read to universe

- - box.schema.user.grant('myuser', 'execute', 'role', 'super')
  - ['role']['super']['execute'] = true
  - equivalent to granting a role to myuser

]]--

local function privileges_from_box(privileges)
    privileges = privileges or {}
    assert(type(privileges) == 'table')

    local res = {
        ['user'] = {},
        ['role'] = {},
        ['space'] = {},
        ['function'] = {},
        ['sequence'] = {},
        ['universe'] = {},
    }

    for _, priv in ipairs(privileges) do
        local perms, obj_type, obj_name = unpack(priv)
        obj_name = obj_name or ''

        res[obj_type][obj_name] = res[obj_type][obj_name] or {}

        for _, perm in ipairs(perms:split(',')) do
            res[obj_type][obj_name][perm] = true
        end
    end

    return res
end

-- Note that 'all' is considered a special value, meaning all objects of
-- obj_type will be granted this permission. Don't use this function if it
-- may occur in any other meaning, e.g. user defined name.
--
-- Note: `obj_names` can be either an array with objects names or a string
--       with a single one. It could also be `nil`, meaning "do nothing".
local function privileges_add_perm(obj_type, obj_names, perm, intermediate)
    if obj_names == nil then
        return
    end
    if type(obj_names) == 'string' then
        obj_names = {obj_names}
    end

    for _, obj_name  in ipairs(obj_names) do
        if obj_name == 'all' then
            -- '' is a special value, meaning all objects of this obj_type.
            obj_name = ''
        end
        intermediate[obj_type][obj_name] =
            intermediate[obj_type][obj_name] or {}
        intermediate[obj_type][obj_name][perm] = true
    end
end

local function privileges_from_config(config_data)
    local privileges = config_data.privileges or {}
    assert(type(privileges) == 'table')

    local intermediate = {
        ['user'] = {},
        ['role'] = {},
        ['space'] = {},
        ['function'] = {},
        ['sequence'] = {},
        ['universe'] = {},
    }

    for _, priv in ipairs(privileges) do
        for _, perm in ipairs(priv.permissions) do
            if priv.universe then
                privileges_add_perm('universe', 'all', perm, intermediate)
            end
            privileges_add_perm('space', priv.spaces, perm, intermediate)
            privileges_add_perm('function', priv.functions, perm, intermediate)
            privileges_add_perm('sequence', priv.sequences, perm, intermediate)
        end
    end

    local roles = config_data.roles or {}

    for _, role_name in ipairs(roles) do
        -- Unlike spaces, functions and sequences, role is allowed to be
        -- named 'all', so `privileges_add_perm()` isn't used.
        intermediate['role'][role_name] = intermediate['role'][role_name] or {}
        intermediate['role'][role_name]['execute'] = true
    end

    return intermediate
end

-- Intermediate representation is basically a set, so this function subtracts
-- `current` from `target`.
-- Return privileges that are present in `target` but not in `current` as
-- a list in the following format:
-- - obj_type: my_type
--   obj_name: my_name
--   privs:
--    - read
--    - write
--    - ...
--
local function privileges_subtract(target, current)
    local lacking = {}

    for obj_type, privileges_group in pairs(target) do
        for obj_name, privileges in pairs(privileges_group) do
            local lacking_privs = {}
            for priv, target_val in pairs(privileges) do
                if target_val and (current[obj_type][obj_name] == nil or
                                   not current[obj_type][obj_name][priv]) then
                    table.insert(lacking_privs, priv)
                end
            end
            if next(lacking_privs) then
                table.insert(lacking, {
                    obj_type = obj_type,
                    obj_name = obj_name,
                    privs = lacking_privs,
                })
            end
        end
    end

    return lacking
end

local function privileges_add_defaults(name, role_or_user, intermediate)
    local res = table.deepcopy(intermediate)

    if role_or_user == 'user' then
        if name == 'guest' then
            privileges_add_perm('role', 'public', 'execute', res)

            privileges_add_perm('universe', '', 'session', res)
            privileges_add_perm('universe', '', 'usage', res)

        elseif name == 'admin' then
            privileges_add_perm('universe', '', 'read', res)
            privileges_add_perm('universe', '', 'write', res)
            privileges_add_perm('universe', '', 'execute', res)
            privileges_add_perm('universe', '', 'session', res)
            privileges_add_perm('universe', '', 'usage', res)
            privileges_add_perm('universe', '', 'create', res)
            privileges_add_perm('universe', '', 'drop', res)
            privileges_add_perm('universe', '', 'alter', res)
            privileges_add_perm('universe', '', 'reference', res)
            privileges_add_perm('universe', '', 'trigger', res)
            privileges_add_perm('universe', '', 'insert', res)
            privileges_add_perm('universe', '', 'update', res)
            privileges_add_perm('universe', '', 'delete', res)

        else
            -- Newly created user:
            privileges_add_perm('role', 'public', 'execute', res)

            privileges_add_perm('universe', '', 'session', res)
            privileges_add_perm('universe', '', 'usage', res)

            privileges_add_perm('user', name, 'alter', res)
        end

    elseif role_or_user == 'role' then
        -- luacheck: ignore 542 empty if branch
        if name == 'public' then
            privileges_add_perm('function', 'box.schema.user.info', 'execute',
                                res)
            privileges_add_perm('function', 'LUA', 'read', res)

            privileges_add_perm('space', '_vcollation', 'read', res)
            privileges_add_perm('space', '_vspace', 'read', res)
            privileges_add_perm('space', '_vsequence', 'read', res)
            privileges_add_perm('space', '_vindex', 'read', res)
            privileges_add_perm('space', '_vfunc', 'read', res)
            privileges_add_perm('space', '_vuser', 'read', res)
            privileges_add_perm('space', '_vpriv', 'read', res)
            privileges_add_perm('space', '_vspace_sequence', 'read', res)

            privileges_add_perm('space', '_truncate', 'write', res)

            privileges_add_perm('space', '_session_settings', 'read', res)
            privileges_add_perm('space', '_session_settings', 'write', res)

        elseif name == 'replication' then
            privileges_add_perm('space', '_cluster', 'write', res)
            privileges_add_perm('universe', '', 'read', res)

        elseif name == 'super' then
            privileges_add_perm('universe', '', 'read', res)
            privileges_add_perm('universe', '', 'write', res)
            privileges_add_perm('universe', '', 'execute', res)
            privileges_add_perm('universe', '', 'session', res)
            privileges_add_perm('universe', '', 'usage', res)
            privileges_add_perm('universe', '', 'create', res)
            privileges_add_perm('universe', '', 'drop', res)
            privileges_add_perm('universe', '', 'alter', res)
            privileges_add_perm('universe', '', 'reference', res)
            privileges_add_perm('universe', '', 'trigger', res)
            privileges_add_perm('universe', '', 'insert', res)
            privileges_add_perm('universe', '', 'update', res)
            privileges_add_perm('universe', '', 'delete', res)

        else
            -- Newly created role has NO permissions.
        end
    else
        assert(false, 'neither role nor user provided')
    end

    return res
end

-- The privileges synchronization between A and B is performed in three steps:
-- 1. Grant all privileges that are present in B,
--    but not present in A (`grant(B - A)`).
-- 2. Add default privileges to B (`B = B + defaults`).
-- 3. Revoke all privileges that are not present in B,
--    but present in A (`revoke(A - B)).
--
-- Default privileges are not granted on step 1, so they stay revoked if
-- revoked manually (e.g. with `box.schema.{user,role}.revoke()`).
-- However, defaults should never be revoked, so target state B is enriched
-- with them before step 3.
local function sync_privileges(name, config_privileges, role_or_user)
    assert(role_or_user == 'user' or role_or_user == 'role')
    log.verbose('syncing privileges for %s %q', role_or_user, name)

    local grant_f = function(name, privs, obj_type, obj_name)
        privs = table.concat(privs, ',')
        log.debug('credentials.apply: ' .. role_or_user ..
                  '.grant(%q, %q, %q, %q)', name, privs, obj_type, obj_name)
        box.schema[role_or_user].grant(name, privs, obj_type, obj_name)
    end
    local revoke_f = function(name, privs, obj_type, obj_name)
        privs = table.concat(privs, ',')
        log.debug('credentials.apply: ' .. role_or_user ..
                  '.revoke(%q, %q, %q, %q)', name, privs, obj_type, obj_name)
        box.schema[role_or_user].revoke(name, privs, obj_type, obj_name)
    end

    local box_privileges = box.schema[role_or_user].info(name)

    config_privileges = privileges_from_config(config_privileges)
    box_privileges = privileges_from_box(box_privileges)

    local grants = privileges_subtract(config_privileges, box_privileges)

    for _, to_grant in ipairs(grants) do
        grant_f(name, to_grant.privs, to_grant.obj_type, to_grant.obj_name)
    end

    config_privileges = privileges_add_defaults(name, role_or_user,
                                                config_privileges)

    local revokes = privileges_subtract(box_privileges, config_privileges)

    for _, to_revoke in ipairs(revokes) do
        revoke_f(name, to_revoke.privs, to_revoke.obj_type, to_revoke.obj_name)
    end

end

-- {{{ Create roles

local function create_role(role_name)
    if box.schema.role.exists(role_name) then
        log.verbose('credentials.apply: role %q already exists', role_name)
    else
        box.schema.role.create(role_name)
    end
end

-- Create roles, grant them permissions and assign underlying
-- roles.
local function create_roles(role_map)
    if role_map == nil then
        return
    end

    for role_name, role_def in pairs(role_map or {}) do
        if role_def ~= nil then
            create_role(role_name)
        end
    end

    -- Sync privileges and assign underlying roles.
    for role_name, role_def in pairs(role_map or {}) do
        if role_def ~= nil then
            sync_privileges(role_name, role_def, 'role')
        end
    end
end

-- }}} Create roles

-- {{{ Create users

local function create_user(user_name)
    if box.schema.user.exists(user_name) then
        log.verbose('credentials.apply: user %q already exists', user_name)
    else
        log.verbose('credentials.apply: create user %q', user_name)
        box.schema.user.create(user_name)
    end
end

local function set_password(user_name, password)
    if password == nil then
        if user_name == 'guest' then
            -- Guest can't have a password, so this is valid and there
            -- is nothing to do.
            return
        end

        local auth_def = box.space._user.index.name:get({user_name})[5]
        if next(auth_def) == nil then
            -- No password is currently set, there is nothing to do.
            log.verbose('credentials.apply: user %q already has no password',
                        user_name)
            return
        end

        log.verbose('credentials.apply: remove password for user %q', user_name)
        -- No password is set, so remove it for the user.
        -- There is no handy function for it, so remove it directly in
        -- system space _user. Users are described in the following way:
        --
        -- - [32, 1, 'myusername', 'user', {'chap-sha1': '<password_hash>'},
        --          [], 1692279984]
        --
        -- The command below overrides with empty map the fifth tuple field,
        -- containing hash (with auth_type == 'chap-sha1' and hash with salt
        -- with auth_type == 'pap-sha256').
        box.space._user.index.name:update(user_name,
            {{'=', 5, setmetatable({}, {__serialize = 'map'})}})

        return
    end

    if user_name == 'guest' then
        error('Setting a password for the guest user is not allowed')
    end

    local auth_def = box.space._user.index.name:get({user_name})[5]
    if next(auth_def) == nil then
        -- No password is currently set for the user, just set a new one.
        log.verbose('credentials.apply: set a password for user %q', user_name)
        box.schema.user.passwd(user_name, password)
        return
    end

    local auth_type = auth_def['chap-sha1'] and 'chap-sha1' or 'pap-sha256'

    if auth_type == 'chap-sha1' then
        local current_hash = auth_def['chap-sha1']

        local new_hash = box.schema.user.password(password)
        if new_hash == current_hash then
            log.verbose('credentials.apply: a password is already set ' ..
                        'for user %q', user_name)
        else
            log.verbose('credentials.apply: set a password for user %q',
                        user_name)
            box.schema.user.passwd(user_name, password)
        end
    else
        assert(auth_def['pap-sha256'])
        local current_salt = auth_def['pap-sha256'][1]
        local current_hash = auth_def['pap-sha256'][2]

        local new_hash = digest.sha256(current_salt .. password)
        if new_hash == current_hash then
            log.verbose('credentials.apply: a password is already set ' ..
                        'for user %q', user_name)
        else
            log.verbose('credentials.apply: set a password for user %q',
                        user_name)
            -- Note: passwd() generated new random salt, it will be different
            -- from current_salt.
            box.schema.user.passwd(user_name, password)
        end
    end
end

-- Create users, set them passwords, assign roles, grant
-- permissions.
local function create_users(user_map)
    if user_map == nil then
        return
    end

    for user_name, user_def in pairs(user_map or {}) do
        if user_def ~= nil then
            create_user(user_name)
            set_password(user_name, user_def.password)
            sync_privileges(user_name, user_def, 'user')
        end
    end
end

-- }}} Create users

local function apply(config)
    local configdata = config._configdata
    local credentials = configdata:get('credentials')
    if credentials == nil then
        return
    end

    -- TODO: What if all the instances in a replicaset are
    -- read-only at configuration applying? They all will ignore
    -- the section and so it will never be applied -- even when
    -- some instance goes to RW.
    --
    -- Moreover, this skip is silent: no log warnings or issue
    -- reporting.
    --
    -- OTOH, a replica (downstream) should ignore all the config
    -- data that is persisted.
    --
    -- A solution could be postpone applying of such data till
    -- RW state. The applying should check that the data is not
    -- added/updated already (arrived from master).
    if box.info.ro then
        log.verbose('credentials.apply: skip the credentials section, ' ..
            'because the instance is in the read-only mode')
        return
    end

    -- Tarantool has the following roles and users present by default on every
    -- instance:
    --
    -- Default roles:
    -- * super
    -- * public
    -- * replication
    --
    -- Default users:
    -- * guest
    -- * admin
    --
    -- These roles and users have according privileges pre-granted by design.
    -- Credentials applier adds such privileges with `priviliges_add_default()`
    -- when syncing. So, for the excessive (non-default) privs to be removed,
    -- these roles and users must be present inside configuration at least in a
    -- form of an empty table. Otherwise, the privileges will be left unchanged,
    -- similar to all used-defined roles and users.

    credentials.roles = credentials.roles or {}
    credentials.roles['super'] = credentials.roles['super'] or {}
    credentials.roles['public'] = credentials.roles['public'] or {}
    credentials.roles['replication'] = credentials.roles['replication'] or {}

    credentials.users = credentials.users or {}
    credentials.users['guest'] = credentials.users['guest'] or {}
    credentials.users['admin'] = credentials.users['admin'] or {}

    -- Create roles and users and synchronise privileges for them.
    create_roles(credentials.roles)
    create_users(credentials.users)
end

return {
    name = 'credentials',
    apply = apply,
    -- Exported for testing purposes.
    _internal = {
        privileges_from_box = privileges_from_box,
        privileges_from_config = privileges_from_config,
        privileges_subtract = privileges_subtract,
        privileges_add_defaults = privileges_add_defaults,
        sync_privileges = sync_privileges,
        set_password = set_password,
    },
}
