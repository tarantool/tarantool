local log = require('internal.config.utils.log')
local digest = require('digest')
local fiber = require('fiber')

-- Var is set with the first apply() call.
local config

-- {{{ Sync helpers

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
 - 'lua_eval'
 - 'lua_call'
 - 'sql'

Note that 'lua_eval', 'lua_call', 'sql' and 'universe' are special,
they don't allow obj_name specialisation.

obj_names:
 - mostly user defined strings, provided by config or box
 - special value '', when there is no obj_name, e.g. for
   'universe' obj_type or for granting permission for all
   objects of a type.

privs:
 - read
 - write
 - execute
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
        ['lua_eval'] = {},
        ['lua_call'] = {},
        ['sql'] = {},
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
        ['lua_eval'] = {},
        ['lua_call'] = {},
        ['sql'] = {},
    }

    for _, priv in ipairs(privileges) do
        for _, perm in ipairs(priv.permissions) do
            if priv.universe then
                privileges_add_perm('universe', 'all', perm, intermediate)
            end
            if priv.lua_eval then
                privileges_add_perm('lua_eval', 'all', perm, intermediate)
            end
            privileges_add_perm('space', priv.spaces, perm, intermediate)
            privileges_add_perm('function', priv.functions, perm, intermediate)
            privileges_add_perm('sequence', priv.sequences, perm, intermediate)
            privileges_add_perm('lua_call', priv.lua_call, perm, intermediate)
            privileges_add_perm('sql', priv.sql, perm, intermediate)
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

-- Get the latest credentials configuration from the config and add empty
-- default users and roles configuration, if they are missing.
local function get_credentials(config)
    local configdata = config._configdata
    local credentials = configdata:get('credentials')

    -- If credentials section in config is empty, skip applier.
    if credentials == nil then
        return {}
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

    return credentials
end

-- }}} Sync helpers

-- {{{ Triggers for grants after obj creation

-- This is fiber channel that contains all scheduled tasks
-- in form of a table with the following format:
-- {type = 'BLOCKING_FULL_SYNC'/'BACKGROUND_FULL_SYNC'/obj_type,
--  name = obj_name}
local sync_tasks

-- This is a map of all objects that are mentioned in the config.
-- It is always in sync with the latest reload and has the following format:
-- [obj_type][obj_name] = true/nil
local target_object_map = {
    ['space'] = {},
    ['function'] = {},
    ['sequence'] = {},
}

-- Iterate through requests and sync when the according flag is set.
local function on_commit_trigger(iterator)
    for _, old_obj, new_obj, space_id in iterator() do

        local obj_type
        if space_id == box.schema.SPACE_ID then
            obj_type = 'space'
        elseif space_id == box.schema.FUNC_ID then
            obj_type = 'function'
        elseif space_id == box.schema.SEQUENCE_ID then
            obj_type = 'sequence'
        else
            goto skip
        end

        if new_obj == nil then
            goto skip
        end

        -- Check if the request is for obj creation or obj rename.
        if old_obj == nil or old_obj.name ~= new_obj.name then
            local obj_name = new_obj.name

            -- Check that the object is present inside config.
            if target_object_map[obj_type][obj_name] then
                if not sync_tasks:is_full() then
                    sync_tasks:put({name = obj_name, type = obj_type})
                else
                    -- In some rare cases the channel can fill up.
                    -- It means that a great number of objects requires
                    -- sync. Thus perform a full synchronization.
                    while not sync_tasks:is_empty() do
                        sync_tasks:get()
                    end
                    sync_tasks:put({type = 'BACKGROUND_FULL_SYNC'})
                end
            end
        end

        ::skip::
    end
end

-- Check that request is space/function/sequence creation or rename
-- and set on_commit_trigger for the transaction.
local function on_replace_trigger(old_obj, new_obj, obj_type, request_type)
    if box.session.type() == 'applier' then
        -- The request is caused by replication, so the instance is in
        -- RO mode. Thus, skip all applier actions.
        return
    end

    if obj_type == '_space' then
        obj_type = 'space'
    elseif obj_type == '_func' then
        obj_type = 'function'
    elseif obj_type == '_sequence' then
        obj_type = 'sequence'
    else
        return
    end

    -- Check if the request is object creation or rename
    if new_obj == nil then
        return
    end

    if not ((request_type == 'INSERT' and old_obj == nil) or
            (request_type == 'UPDATE' and old_obj ~= nil and
                old_obj.name ~= new_obj.name)) then
        return
    end

    local obj_name = new_obj.name
    -- Check that the object is present inside config.
    if not target_object_map[obj_type][obj_name] then
        return
    end

    -- Set the on_commit trigger to apply the action. Action could not
    -- be applied at this point because the object may not be fully
    -- registered.
    box.on_commit(on_commit_trigger)
end

-- }}} Triggers for grants after obj creation

-- {{{ Main sync logic

-- Grant or revoke some user/role privileges for an object.
local privileges_action_f = function(grant_or_revoke, role_or_user, name, privs,
                                     obj_type, obj_name)
    assert(grant_or_revoke == 'grant' or grant_or_revoke == 'revoke')
    privs = table.concat(privs, ',')

    -- Try to apply the action immediately. If the object doesn't exist,
    -- the sync will be applied inside the trigger on object creation/rename.
    local ok, err = pcall(box.schema[role_or_user][grant_or_revoke],
                          name, privs, obj_type, obj_name)

    if ok then
        log.verbose('credentials.apply: box.schema.%s.%s(%q, %q, %q, %q)',
                    role_or_user, grant_or_revoke, name, privs,
                    obj_type, obj_name)
        return
    end
    if err.code ~= box.error.NO_SUCH_SPACE and
            err.code ~= box.error.NO_SUCH_FUNCTION and
            err.code ~= box.error.NO_SUCH_SEQUENCE then
        err = ('credentials.apply: box.schema.%s.%s(%q, %q, %q, %q) failed: %s')
              :format(role_or_user, grant_or_revoke, name, privs, obj_type,
                      obj_name, err)
        config:_alert({type = 'error', message = err})
    else
        local msg = "credentials.apply: %s %q hasn't been created yet, " ..
                    "'box.schema.%s.%s(%q, %q, %q, %q)' will be applied later"
        msg = msg:format(obj_type, obj_name, role_or_user, grant_or_revoke,
                         name, privs, obj_type, obj_name)
        config:_alert({type = 'warn', message = msg})
    end
end

-- Perform a synchronization of privileges for all users/roles.
-- obj_to_sync is an optional parameter, if it is non-nil,
-- the sync is being performed only for the provided object.
local function sync_privileges(credentials, obj_to_sync)
    assert(obj_to_sync == nil or type(obj_to_sync) == 'table')

    if obj_to_sync then
        log.verbose('credentials.apply: syncing privileges for %s %q',
                    obj_to_sync.type, obj_to_sync.name)
    end

    -- The privileges synchronization between A and B is performed in 3 steps:
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
    local function sync(role_or_user, name, config_privileges)
        assert(role_or_user == 'user' or role_or_user == 'role')
        if not obj_to_sync then
            log.verbose('credentials.apply: syncing privileges for %s %q',
                        role_or_user, name)
        end

        local box_privileges = box.schema[role_or_user].info(name)

        config_privileges = privileges_from_config(config_privileges)
        box_privileges = privileges_from_box(box_privileges)

        local grants = privileges_subtract(config_privileges, box_privileges)

        for _, to_grant in ipairs(grants) do
            -- Note that grants are filtered per object, if required.
            if obj_to_sync == nil or (obj_to_sync.type == to_grant.obj_type and
                                     obj_to_sync.name == to_grant.obj_name) then
                privileges_action_f('grant', role_or_user, name, to_grant.privs,
                                    to_grant.obj_type, to_grant.obj_name)
            end
        end

        config_privileges = privileges_add_defaults(name, role_or_user,
                                                    config_privileges)

        local revokes = privileges_subtract(box_privileges, config_privileges)

        for _, to_revoke in ipairs(revokes) do
            -- Note that revokes are filtered per object, if required.
            if obj_to_sync == nil or (obj_to_sync.type == to_revoke.obj_type and
                                    obj_to_sync.name == to_revoke.obj_name) then
                privileges_action_f('revoke', role_or_user, name,
                                    to_revoke.privs, to_revoke.obj_type,
                                    to_revoke.obj_name)
            end
        end
    end

    -- Note that the logic inside the sync() function, i.e. privileges dump
    -- from box, diff calculation and grants/revokes should happen inside one
    -- transaction.

    for name, user_def in pairs(credentials.users or {}) do
        sync('user', name, user_def)
    end

    for name, role_def in pairs(credentials.roles or {}) do
        sync('role', name, role_def)
    end
end

-- }}} Main sync logic

-- {{{ Create roles

local function create_role(role_name)
    if box.schema.role.exists(role_name) then
        log.verbose('credentials.apply: role %q already exists', role_name)
    else
        log.verbose('credentials.apply: create role %q', role_name)
        box.schema.role.create(role_name)
    end
end

local function create_roles(role_map)
    for role_name, role_def in pairs(role_map or {}) do
        if role_def ~= nil then
            create_role(role_name)
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
        config:_alert({type = 'error',
            message = 'credentials.apply: setting a password for ' ..
                      'the guest user is not allowed'})
    end

    local auth_def = box.space._user.index.name:get({user_name})[5]
    if next(auth_def) == nil then
        -- No password is currently set for the user, just set a new one.
        log.verbose('credentials.apply: set a password for user %q', user_name)
        box.schema.user.passwd(user_name, password)
        return
    end

    local auth_type = auth_def['chap-sha1'] and 'chap-sha1' or 'pap-sha256'

    local new_password = false

    if auth_type == 'chap-sha1' then
        local current_hash = auth_def['chap-sha1']

        local new_hash = box.schema.user.password(password)
        if new_hash ~= current_hash then
            new_password = true
        end
    else
        assert(auth_def['pap-sha256'])
        local current_salt = auth_def['pap-sha256'][1]
        local current_hash = auth_def['pap-sha256'][2]

        local new_hash = digest.sha256(current_salt .. password)
        if new_hash == current_hash then
            -- Note: passwd() generated new random salt, it will be different
            -- from current_salt.
            new_password = true
        end
    end

    if not new_password then
        -- Note that security.auth_type is applied by box_cfg applier.
        -- It is executed before credentials applier, so the current
        -- box.cfg.auth_type is already set.
        if box.cfg.auth_type == auth_type then
            log.verbose('credentials.apply: a password is already set ' ..
                        'for user %q', user_name)
        else
            log.verbose('credentials.apply: a password for user %q has ' ..
                        'different auth_type, resetting it', user_name)
            box.schema.user.passwd(user_name, password)
        end
    else
        log.verbose('credentials.apply: set a password for user %q',
                    user_name)
        box.schema.user.passwd(user_name, password)
    end

end

local function create_users(user_map)
    for user_name, user_def in pairs(user_map or {}) do
        if user_def ~= nil then
            create_user(user_name)
            set_password(user_name, user_def.password)
        end
    end
end

-- }}} Create users

-- {{{ Applier

-- Objects that are inside the config must be registered in target_object_map,
-- so they will trigger synchronization every time an object with such name
-- is created or is being renamed to the config-provided name.
local function register_objects(users_or_roles_config)
    for _name, config in pairs(users_or_roles_config or {}) do
        local privileges = privileges_from_config(config)
        for obj_type in pairs(target_object_map) do
            for obj_name in pairs(privileges[obj_type] or {}) do
                target_object_map[obj_type][obj_name] = true
            end
        end
    end
end

-- Fiber channel for resulting message to block execution
-- of main fiber in case of 'BLOCKING_FULL_SYNC'.
local wait_sync

-- The worker gets synchronization commands from sync_tasks.
-- If the instance is in RO mode, the worker will wait for it
-- to be switched to RW.
-- Possible commands:
-- {type = 'BLOCKING_FULL_SYNC'} - perform a full sync, write message
--                                 to `wait_sync` on return
-- {type = 'BACKGROUND_FULL_SYNC'} - perform a full sync, skip return message
-- {type = obj_type, name = obj_name} - perform a per-object sync, skip
--                                      return message
local function sync_credentials_worker()
    while true do
        local obj_to_sync = sync_tasks:get()

        if obj_to_sync.type == 'BLOCKING_FULL_SYNC' or
                obj_to_sync.type == 'BACKGROUND_FULL_SYNC' then

            if box.info.ro then
                local msg = 'credentials.apply: Tarantool is in Read Only ' ..
                            'mode, so credentials will be set up in the ' ..
                            'background when it is switched to Read Write mode'
                config:_alert({type = 'warn', message = msg})
                box.ctl.wait_rw()
            end

            -- On config reload, drop the old list of registered objects.
            target_object_map = {
                ['space'] = {},
                ['function'] = {},
                ['sequence'] = {},
            }

            local credentials = get_credentials(config)

            register_objects(credentials.roles)
            register_objects(credentials.users)

            box.atomic(function()
                create_roles(credentials.roles)
                create_users(credentials.users)

                sync_privileges(credentials)
            end)

            if obj_to_sync.type == 'BLOCKING_FULL_SYNC' then
                wait_sync:put('Done')
            end
        else
            local credentials = get_credentials(config)

            box.atomic(sync_privileges, credentials, obj_to_sync)
        end
    end
end

-- Credentials are being synced inside this dedicated fiber worker.
local sync_credentials_fiber

-- One-shot flag. It indicates that on_replace triggers are
-- set for box.space._space/_func/_sequence.
local triggers_are_set

local function apply(config_module)
    config = config_module

    -- Create a fiber channel for scheduled tasks for sync worker.
    if not sync_tasks then
        -- There is no good reason to have the channel capacity limited,
        -- but tarantool fiber channels have an upper limit by design.
        -- Thus, in rare cases when the channel is full, the mandatory
        -- full sync is executed, dropping all other scheduled tasks.
        sync_tasks = fiber.channel(1024)
    else
        -- Clear scheduled syncs on reload. Full sync will be executed
        -- afterwards anyway.
        while not sync_tasks:is_empty() do
            sync_tasks:get()
        end
    end

    if not wait_sync then
        wait_sync = fiber.channel()
    end

    -- Set trigger on after space/function/sequence creation.
    if not triggers_are_set then
        triggers_are_set = true
        box.space._space:on_replace(on_replace_trigger)
        box.space._func:on_replace(on_replace_trigger)
        box.space._sequence:on_replace(on_replace_trigger)
    end

    -- Fiber is required here to have the credentials synced in the background
    -- when Tarantool is in Read Only mode after it is switched to RW.

    -- Note that only one fiber exists at a time.
    if sync_credentials_fiber == nil or
            sync_credentials_fiber:status() == 'dead' then
        sync_credentials_fiber = fiber.new(sync_credentials_worker)
    end

    -- If Tarantool is already in Read Write mode, credentials are still
    -- applied in the fiber to avoid possible concurrency issues, but the
    -- main applier fiber is blocked by `wait_sync`.
    if not box.info.ro then
        -- Schedule a full sync with a result message on return.
        sync_tasks:put({type = 'BLOCKING_FULL_SYNC'})

        wait_sync:get()
    else
        -- Schedule a full sync in the background. It will be executed
        -- when the instance switches to RW.
        sync_tasks:put({type = 'BACKGROUND_FULL_SYNC'})
    end
end

-- }}} Applier

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
