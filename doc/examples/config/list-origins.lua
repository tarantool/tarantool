-- List non-system users and roles with their origins and print drop commands
-- for all non-config origins (e.g. manual/"").

local CONFIG_ORIGIN   = 'config'
local DEFAULT_ORIGIN  = ''   -- "manual" origin
local USER_OPTS_FIELD = 8    -- _user[8] = { origins = { ... } }

-- System objects to skip.
local SYSTEM_USERS = { admin = true, guest = true }
local SYSTEM_ROLES = { super = true, public = true, replication = true }

-- Return origins for user/role from the given tuple.
local function user_origins_from_tuple(tuple)
    if tuple == nil then
        return {[DEFAULT_ORIGIN] = true}
    end
    if tuple[USER_OPTS_FIELD] == nil or
       tuple[USER_OPTS_FIELD].origins == nil then
        return {[DEFAULT_ORIGIN] = true}
    end
    return tuple[USER_OPTS_FIELD].origins
end

local function sorted_keys(map)
    local keys = {}
    for k in pairs(map) do table.insert(keys, k) end
    table.sort(keys)
    return keys
end

local function describe_origin(origin)
    if origin == DEFAULT_ORIGIN then
        return 'manual'
    end
    return origin
end

local function drop_command(name, origin, is_role)
    local object = is_role and 'role' or 'user'
    return ('box.schema.%s.drop(%q)')
           :format(object, name, origin)
end

local function non_config_origins(origins)
    local res = {}
    for k in pairs(origins) do
        if k ~= CONFIG_ORIGIN then table.insert(res, k) end
    end
    table.sort(res)
    return res
end

local all_users, all_roles = {}, {}
local drop_users, drop_roles = {}, {}

for _, t in box.space._user:pairs() do
    local name, typ = t.name, t.type

    -- Skip system users/roles explicitly.
    if (typ == 'user' and SYSTEM_USERS[name]) or
       (typ == 'role' and SYSTEM_ROLES[name]) then
        goto continue
    end

    local origins = user_origins_from_tuple(t)
    local keys    = sorted_keys(origins)

    local labels = {}
    for _, o in ipairs(keys) do
        table.insert(labels, describe_origin(o))
    end

    local line = ('%s (origins: %s)'):format(name, table.concat(labels, ', '))

    if typ == 'user' then
        table.insert(all_users, line)
        for _, o in ipairs(non_config_origins(origins)) do
            table.insert(drop_users, drop_command(name, o, false))
        end
    elseif typ == 'role' then
        table.insert(all_roles, line)
        for _, o in ipairs(non_config_origins(origins)) do
            table.insert(drop_roles, drop_command(name, o, true))
        end
    end

    ::continue::
end

local function print_list(title, list)
    if #list == 0 then return end
    print(title)
    for _, s in ipairs(list) do
        print('  ' .. s)
    end
    print()
end

print_list('All non-system users:', all_users)
print_list('All non-system roles:', all_roles)

if #drop_users > 0 then
    print('Drop commands for non-config origins (users):')
    for _, cmd in ipairs(drop_users) do
        print('  ' .. cmd)
    end
    print()
end

if #drop_roles > 0 then
    print('Drop commands for non-config origins (roles):')
    for _, cmd in ipairs(drop_roles) do
        print('  ' .. cmd)
    end
    print()
end

if #all_users == 0 and #all_roles == 0 then
    print('No non-system users or roles found.')
end
