local expression = require('internal.config.utils.expression')
local log = require('internal.config.utils.log')
local loaders = require('internal.loaders')
local digest = require('digest')
local fiber = require('fiber')
local mkversion = require('internal.mkversion')

-- Var is set with the first apply() call.
local config

local PRIV_OPTS_FIELD_ID = 6
local CONFIG_ORIGIN = 'config'

-- {{{ Sync helpers

local function decode_privilege(privileges_mask)
    local privs_map = {
        read      = box.priv.R,
        write     = box.priv.W,
        execute   = box.priv.X,
        session   = box.priv.S,
        usage     = box.priv.U,
        create    = box.priv.C,
        drop      = box.priv.D,
        alter     = box.priv.A,
        reference = box.priv.REFERENECE,
        trigger   = box.priv.TRIGGER,
        insert    = box.priv.INSERT,
        update    = box.priv.UPDATE,
        delete    = box.priv.DELETE
    }
    local privileges = {}
    for priv_name, priv_mask in pairs(privs_map) do
        if bit.band(privileges_mask, priv_mask) ~= 0 then
            privileges[priv_name] = true
        end
    end
    return privileges
end

local function obj_name_by_type_and_id(obj_type, obj_id)
    if type(obj_id) == 'string' then
        return obj_id
    end
    local singleton_object_types = {
        ['universe'] = true,
        ['lua_eval'] = true,
        ['sql'] = true,
    }
    if singleton_object_types[obj_type] then
        return ''
    end
    if obj_type == 'user' or obj_type == 'role' then
        return box.space._user:get({obj_id}).name
    end
    if obj_type == 'space' then
        return box.space._space:get({obj_id}).name
    end
    if obj_type == 'function' then
        return box.space._func:get({obj_id}).name
    end
    assert(obj_type == 'sequence')
    return box.space._sequence:get({obj_id}).name
end

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

local function privileges_from_box(name)
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

    local grantee = box.space._user.index.name:get({name})
    if grantee == nil then
        return res
    end
    for _, tuple in pairs(box.space._priv:select({grantee.id})) do
        local opts = tuple[PRIV_OPTS_FIELD_ID]
        local privileges = opts ~= nil and opts.origins ~= nil and
              opts.origins[CONFIG_ORIGIN]
        if privileges then
            local obj_type = tuple.object_type
            local obj_name = obj_name_by_type_and_id(obj_type, tuple.object_id)
            res[obj_type][obj_name] = decode_privilege(privileges)
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

--
-- Create a credential sharding role if it is assigned to any user or role and
-- has not been declared in 'credentials.roles'.
--
-- Replicasets with the sharding storage role must be assigned the credentials
-- sharding role. In these replicasets, the role will be assigned all necessary
-- privileges.
--
-- For replicasets that do not have the sharding storage role assigned, the
-- credentials sharding role will not have any privileges.
--
-- If the snapshot version does not match the Tarantool schema version, DDL
-- operations, including creating roles and granting privileges, are prohibited.
-- For user-created changes, this will throw an error as expected, but since the
-- "sharding" role is created by default, the error may still appear even if the
-- user does not make any changes. To partially avoid this problem, we do not
-- create the "sharding" role if it is not in use. This is a temporary solution.
local function sharding_role(configdata)
    local roles = configdata:get('credentials.roles')
    local users = configdata:get('credentials.users')
    local has_sharding_role = false
    for _, role in pairs(roles or {}) do
        for _, role_role in pairs(role.roles or {}) do
            has_sharding_role = has_sharding_role or role_role == 'sharding'
        end
    end
    for _, user in pairs(users or {}) do
        for _, user_role in pairs(user.roles or {}) do
            has_sharding_role = has_sharding_role or user_role == 'sharding'
        end
    end
    if not has_sharding_role then
        return
    end

    -- Add necessary privileges if storage sharding role is enabled.
    local sharding_roles = configdata:get('sharding.roles')
    if sharding_roles == nil or #sharding_roles == 0 then
        return {}
    end
    local is_storage = false
    for _, role in pairs(sharding_roles) do
        is_storage = is_storage or role == 'storage'
    end
    if not is_storage then
        return {}
    end

    local funcs = {}
    --
    -- The error will be thrown later, in sharding.lua. Here we are simply
    -- trying to avoid the "module not found" error.
    --
    local ok, vshard = pcall(loaders.require_first, 'vshard-ee', 'vshard')
    if ok and expression.eval('v >= 0.1.25', {v = vshard.consts.VERSION}) then
        local vexports = loaders.require_first('vshard-ee.storage.exports',
            'vshard.storage.exports')
        local exports = vexports.compile(vexports.log[#vexports.log])
        for name in pairs(exports.funcs) do
            table.insert(funcs, name)
        end
    end
    return {
        privileges = {{
            permissions = {'execute'},
            functions = funcs,
        }},
        roles = {'replication'},
    }
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
    -- These roles are added to the credentials to ensure that if a default
    -- user or a default role was granted some privileges by the config,
    -- those privileges would be dropped when the role/user is removed from
    -- the config.

    credentials.roles = credentials.roles or {}
    credentials.roles['super'] = credentials.roles['super'] or {}
    credentials.roles['public'] = credentials.roles['public'] or {}
    credentials.roles['replication'] = credentials.roles['replication'] or {}

    -- Add a semi-default role 'sharding'.
    credentials.roles['sharding'] = credentials.roles['sharding'] or
                                    sharding_role(configdata)

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
    ['role'] = {},
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
        elseif space_id == box.schema.USER_ID then
            obj_type = 'role'
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
    elseif obj_type == '_user' then
        obj_type = 'role'
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
                          name, privs, obj_type, obj_name,
                          {_origin = CONFIG_ORIGIN})

    if ok then
        log.verbose('credentials.apply: box.schema.%s.%s(%q, %q, %q, %q)',
                    role_or_user, grant_or_revoke, name, privs,
                    obj_type, obj_name)
        return
    end
    if err.code ~= box.error.NO_SUCH_SPACE and
            err.code ~= box.error.NO_SUCH_ROLE and
            err.code ~= box.error.NO_SUCH_FUNCTION and
            err.code ~= box.error.NO_SUCH_SEQUENCE then
        err = ('credentials.apply: box.schema.%s.%s(%q, %q, %q, %q) failed: %s')
              :format(role_or_user, grant_or_revoke, name, privs, obj_type,
                      obj_name, err)
        config._aboard:set({type = 'error', message = err})
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

    -- Prepare to drop missed privilege alerts.
    --
    -- The actual alerts will be issued in the sync() function
    -- below.
    --
    -- Important: we don't follow the `obj_to_sync` filter here
    -- for simplicity. Just revisit all the missed privilege
    -- alerts: mark all of them and issue again the actual ones.
    -- The actual drop will occur later. New alerts, if any, are
    -- issued before the drop.
    config._aboard:each(function(_key, alert)
        if alert._trait == 'missed_privilege' then
            alert._trait = 'missed_privilege_obsolete'
        end
    end)

    -- The privileges synchronization performed in 2 steps:
    -- 1. Grant all privileges that are present in config and were not granted
    --    by the config before.
    -- 2. Revoke all privileges that are not present in config and were granted
    --    by the config before.
    local function sync(role_or_user, name, config_privileges)
        assert(role_or_user == 'user' or role_or_user == 'role')
        if not obj_to_sync then
            log.verbose('credentials.apply: syncing privileges for %s %q',
                        role_or_user, name)
        end

        local box_privileges = privileges_from_box(name)
        config_privileges = privileges_from_config(config_privileges)

        local grants = privileges_subtract(config_privileges, box_privileges)

        for _, to_grant in ipairs(grants) do
            -- Note that grants are filtered per object, if required.
            if obj_to_sync == nil or (obj_to_sync.type == to_grant.obj_type and
                                     obj_to_sync.name == to_grant.obj_name) then
                privileges_action_f('grant', role_or_user, name, to_grant.privs,
                                    to_grant.obj_type, to_grant.obj_name)
            end
        end

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

        -- Collect updated information about privileges in the
        -- database and recalculate the difference from the target
        -- configuration.
        box_privileges = privileges_from_box(name)
        local missed_grants = privileges_subtract(config_privileges,
                                                  box_privileges)

        -- The most frequent scenario is when a privilege
        -- couldn't be granted, because the object (space/
        -- function/sequence) doesn't exist at the moment.
        --
        -- That's normal and the alert will be dropped on next
        -- sync_privileges() call. If the object appears and the
        -- privileges from the configuration are given, the alert
        -- will not be issued again in this call.
        --
        -- If a serious error occurs on the privilege granting
        -- (not a 'no such an object' one), then an alert of the
        -- 'error' type is issued in the privileges_action_f().
        --
        -- The 'error' alerts aren't dropped here. The only way
        -- to get rid of them is to try to re-apply the
        -- configuration using config:reload() (or using automatic
        -- reload from a remote config storage in Tarantool
        -- Enterprise Edition).
        --
        -- Important: this code deliberately ignores the
        -- `obj_to_sync` filter. It should be in-sync with the
        -- :drop_if() call above.
        for _, grant in ipairs(missed_grants) do
            local alert = {
                type = 'warn',
                _trait = 'missed_privilege',
            }
            local msg = 'box.schema.%s.%s(%q, %q, %q, %q) has failed ' ..
                'because either the object has not been created yet, ' ..
                'a database schema upgrade has not been performed, ' ..
                'or the privilege write has failed (separate alert reported)'
            local privs = table.concat(grant.privs, ',')
            alert.message = msg:format(role_or_user, 'grant', name, privs,
                                       grant.obj_type, grant.obj_name)
            config._aboard:set(alert)
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

    -- Drop obsolete missed_privilege alerts.
    config._aboard:drop_if(function(_key, alert)
        return alert._trait == 'missed_privilege_obsolete'
    end)
end

-- The flag indicating the box.space._schema:on_replace trigger
-- is set. It's set when the config is applied if the schema
-- version is not the latest. It's dropped when the schema was
-- upgraded.
local on_schema_replace_trigger_is_set = false

-- The flag indicates if the schema is upgraded to the latest
-- version. It's impossible to use the internal call
-- `box.internal.schema_needs_upgrade()` because at the moment
-- we are granting/revoking privileges its value isn't updated.
local schema_is_upgraded = false

-- The conditional is broadcasted when the schema was upgraded to
-- the current tarantool version.
local schema_is_upgraded_cond

local function update_schema_upgraded_status(version)
    local schema_version = version or mkversion.get()
    schema_is_upgraded = schema_version == mkversion.get_latest()

    if schema_is_upgraded then
        assert(schema_is_upgraded_cond ~= nil)
        schema_is_upgraded_cond:broadcast()
    end
end

local function on_schema_replace_trigger(_, new)
    assert(on_schema_replace_trigger_is_set)

    if new == nil then
        return
    end

    local latest_version = mkversion.get_latest()
    local new_version = mkversion.from_tuple(new)

    if new_version == latest_version then
        box.on_commit(function()
            update_schema_upgraded_status(new_version)
        end)

        on_schema_replace_trigger_is_set = false
        box.space._schema:on_replace(nil, on_schema_replace_trigger)
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
        local message = 'credentials.apply: setting a password for ' ..
                        'the guest user is not allowed'
        config._aboard:set({type = 'error', message = message})
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

local function sync_object(obj_to_sync)
    if obj_to_sync.type == 'BLOCKING_FULL_SYNC' or
        obj_to_sync.type == 'BACKGROUND_FULL_SYNC' then

        if box.info.ro then
            log.verbose('credentials: the database is in the read-only ' ..
                'mode. Waiting for the read-write mode to set up the ' ..
                'credentials given in the configuration. If another ' ..
                'instance in the replicaset will write these new ' ..
                'credentials, this instance will receive them and will ' ..
                'skip further actions.')

            box.ctl.wait_rw()
        end

        -- On config reload, drop the old list of registered objects.
        target_object_map = {
            ['role'] = {},
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

    else
        local credentials = get_credentials(config)

        box.atomic(sync_privileges, credentials, obj_to_sync)
    end
end

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
    fiber.self():name('sync_credentials', { truncate = true })

    if not schema_is_upgraded then
        local not_upgraded_alert_key = 'not_upgraded_schema'

        config._aboard:set({
            type = 'warn',
            message = 'credentials: the database schema has an old version ' ..
                      'and users/roles/privileges cannot be applied. '..
                      'Consider executing box.schema.upgrade() to perform an '..
                      'upgrade.'
        }, {key = not_upgraded_alert_key})

        schema_is_upgraded_cond:wait()
        assert(schema_is_upgraded)

        config._aboard:drop(not_upgraded_alert_key)
    end

    while true do
        local obj_to_sync = sync_tasks:get()

        local _, err = pcall(sync_object, obj_to_sync)

        fiber.testcancel()

        if obj_to_sync.type == 'BLOCKING_FULL_SYNC' then
            wait_sync:put(err or 'Done')
        elseif err then
            log.error(err)
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

    if not schema_is_upgraded_cond then
        schema_is_upgraded_cond = fiber.cond()
    end
    update_schema_upgraded_status()

    if not schema_is_upgraded and not on_schema_replace_trigger_is_set then
        on_schema_replace_trigger_is_set = true
        box.space._schema:on_replace(on_schema_replace_trigger)
    end

    -- Set trigger on after space/function/sequence creation.
    if not triggers_are_set then
        triggers_are_set = true

        box.space._user:on_replace(on_replace_trigger)
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
    if not box.info.ro and schema_is_upgraded then
        -- Schedule a full sync with a result message on return.
        sync_tasks:put({type = 'BLOCKING_FULL_SYNC'})

        local sync_result = wait_sync:get()
        if sync_result ~= 'Done' then
            error(sync_result)
        end
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
        set_config = function(config_module)
            config = config_module
        end,
        privileges_from_box = privileges_from_box,
        privileges_from_config = privileges_from_config,
        privileges_subtract = privileges_subtract,
        sync_privileges = sync_privileges,
        set_password = set_password,
    },
}
