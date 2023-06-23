local log = require('internal.config.utils.log')

local function grant_privileges(name, privileges, role_or_user, grant_f)
    for _, privilege in ipairs(privileges or {}) do
        log.verbose('credentials.apply: grant %s to %s %s (if not exists)',
            privilege, role_or_user, name)
        for _, permission in ipairs(privilege.permissions or {}) do
            local opts = {if_not_exists = true}
            if privilege.universe then
                grant_f(name, permission, 'universe', nil, opts)
            end
            -- TODO: It is not possible to grant a permission for
            -- a non-existing object. It blocks ability to set it
            -- from a config. Disabled for now.
            --[[
            for _, space in ipairs(privilege.spaces or {}) do
                grant_f(name, permission, 'space', space, opts)
            end
            for _, func in ipairs(privilege.functions or {}) do
                grant_f(name, permission, 'function', func, opts)
            end
            for _, seq in ipairs(privilege.sequences or {}) do
                grant_f(name, permission, 'sequence', seq, opts)
            end
            ]]--
        end
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

local function assign_roles_to_role(role_name, roles)
    for _, role in ipairs(roles or {}) do
        log.verbose('credentials.apply: add role %q as underlying for ' ..
            'role %q (if not exists)', role, role_name)
        box.schema.role.grant(role_name, role, nil, nil, {if_not_exists = true})
    end
end

-- Create roles, grant them permissions and assign underlying
-- roles.
local function create_roles(role_map)
    if role_map == nil then
        return
    end

    -- Create roles and grant then permissions. Skip assigning
    -- underlying roles till all the roles will be created.
    for role_name, role_def in pairs(role_map or {}) do
        if role_def ~= nil then
            create_role(role_name)
            grant_privileges(role_name, role_def.privileges, 'role',
                box.schema.role.grant)
        end
    end

    -- Assign underlying roles.
    for role_name, role_def in pairs(role_map or {}) do
        if role_def ~= nil then
            assign_roles_to_role(role_name, role_def.roles)
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
    if password == nil or next(password) == nil then
        if user_name ~= 'guest' then
            log.verbose('credentials.apply: remove password for user %q',
                user_name)
            -- TODO: Check for hashes and if absent remove the password.
        end
    elseif password ~= nil and password.plain ~= nil then
        if user_name == 'guest' then
            error('Setting a password for the guest user has no effect')
        end
        -- TODO: Check if the password can be hashed in somewhere other then
        --       'chap-sha1' or if the select{user_name} may return table of
        --       a different shape.
        local stored_user_def = box.space._user.index.name:get({user_name})
        local stored_hash = stored_user_def[5]['chap-sha1']
        local given_hash = box.schema.user.password(password.plain)
        if given_hash == stored_hash then
            log.verbose('credentials.apply: a password is already set ' ..
                'for user %q', user_name)
        else
            log.verbose('credentials.apply: set a password for user %q',
                user_name)
            box.schema.user.passwd(user_name, password.plain)
        end
    --[[
    elseif sha1() then
    elseif sha256() then
    ]]--
    else
        assert(false)
    end
end

local function assing_roles_to_user(user_name, roles)
    for _, role in ipairs(roles or {}) do
        log.verbose('grant role %q to user %q (if not exists)', role, user_name)
        box.schema.user.grant(user_name, role, nil, nil, {if_not_exists = true})
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
            assing_roles_to_user(user_name, user_def.roles)
            grant_privileges(user_name, user_def.privileges, 'user',
                box.schema.user.grant)
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

    create_roles(credentials.roles)
    create_users(credentials.users)
end

return {
    name = 'credentials',
    apply = apply
}
