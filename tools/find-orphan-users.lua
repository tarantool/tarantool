-- Print commands to drop users and roles that are not managed by
-- the YAML configuration.
--
-- These are ones created from a Lua code using one of the
-- following functions:
--
--  | box.schema.user.create()
--  | box.schema.role.create()
--
-- If users/roles are managed using the YAML configuration, then
-- it is likely that all of them are supposed to be managed this
-- way. If a user/a role is removed from the YAML configuration,
-- the admin expectation is likely that it is deleted from the
-- database.
--
-- However, users/roles created from Lua are not deleted
-- automatically due to compatibility concerns. It is an
-- admin responsibility to run this script and ensure that
-- there are no stalled entries. See [1] for the motivating
-- scenario.
--
-- [1]: https://github.com/tarantool/tarantool/issues/11827
--
-- Note: Users/roles created from the YAML configuration by
-- tarantool versions less than 3.6 can't be distinguished from
-- ones created from Lua and considered as the Lua ones.
--
-- Note: A user/a role can be created from Lua and from the YAML
-- configuration both. In this case box.schema.*.drop() deletes
-- the entry created from Lua. The entry that comes from the YAML
-- configuration remains in the database (until deleted from the
-- configuration).
--
-- Usage:
--
-- dofile('/path/to/this/file.lua')

local bit = require('bit')

local DEFAULT_ORIGIN = ''
local CONFIG_ORIGIN = 'config'
local USER_OPTS_FIELD = 8
local PRIV_OPTS_FIELD = 6

-- Return origins for user/role from the given tuple.
local function user_origins_from_tuple(tuple)
    if tuple[USER_OPTS_FIELD] == nil or
       tuple[USER_OPTS_FIELD].origins == nil then
        return {[DEFAULT_ORIGIN] = true}
    end
    return tuple[USER_OPTS_FIELD].origins
end

local function is_system(tuple)
    return tuple.id <= box.schema.SYSTEM_USER_ID_MAX or
        tuple.id == box.schema.SUPER_ROLE_ID
end

local function disown_command(tuple)
    return ('box.schema.%s.disown(%q)'):format(tuple.type, tuple.name)
end

local function drop_command(tuple)
    return ('box.schema.%s.drop(%q)'):format(tuple.type, tuple.name)
end

local config_entries = {}
local duplicate_entries = {}
local orphan_entries = {}

for _, t in box.space._user:pairs() do
    if not is_system(t) then
        local origins = user_origins_from_tuple(t)
        local comes_from_config = origins[CONFIG_ORIGIN]
        local comes_from_lua = origins[DEFAULT_ORIGIN]

        if comes_from_config then
            table.insert(config_entries, ('%s %q'):format(t.type, t.name))
        end

        if comes_from_lua and comes_from_config then
            table.insert(duplicate_entries, disown_command(t))
        end

        if comes_from_lua and not comes_from_config then
            table.insert(orphan_entries, drop_command(t))
        end
    end
end

local function gen_disown_f(user_or_role)
    assert(user_or_role == 'user' or user_or_role == 'role')

    local func_name = ('box.schema.%s.disown'):format(user_or_role)
    local function e(fmt, ...)
        error(('%s: %s'):format(func_name, fmt:format(...)), 0)
    end

    local function migrate_privileges_to_config(uid)
        for _, tuple in ipairs(box.space._priv.index.primary:select({uid})) do
            local opts = tuple[PRIV_OPTS_FIELD]
            local origins
            if opts ~= nil and opts.origins ~= nil then
                origins = table.deepcopy(opts.origins)
            else
                origins = {[DEFAULT_ORIGIN] = tuple.privilege}
            end
            if origins[DEFAULT_ORIGIN] ~= nil
               and origins[DEFAULT_ORIGIN] ~= 0 then
                origins[CONFIG_ORIGIN] = bit.bor(origins[CONFIG_ORIGIN] or 0,
                    origins[DEFAULT_ORIGIN])
                opts = opts or {}
                opts.origins = origins
                box.space._priv:update({tuple.grantee, tuple.object_type,
                    tuple.object_id}, {{'=', PRIV_OPTS_FIELD, opts}})
            end
        end
    end

    return function(name)
        if type(name) ~= 'string' then
            e('expected string, got %s', type(name))
        end

        local t = box.space._user.index.name:get({name})
        if t == nil then
            e('unable to find %s %q', user_or_role, name)
        end

        local origins = user_origins_from_tuple(t)
        local comes_from_config = origins[CONFIG_ORIGIN]
        local comes_from_lua = origins[DEFAULT_ORIGIN]

        if not comes_from_config then
            e('the %s %q is not owned by the YAML configuration', user_or_role,
                name)
        end

        if not comes_from_lua then
            e('the %s %q is not owned by the Lua code', user_or_role, name)
        end

        -- Temporary compatibility step: migrate previously granted privileges
        -- to CONFIG origin before drop. We don't have centralized privilege
        -- management from YAML yet, so for now we just migrate existing grants.
        -- This prevents privilege loss when upgrading from older Tarantool
        -- versions where earlier grants could otherwise be overwritten.
        migrate_privileges_to_config(t.id)
        box.schema[user_or_role].drop(name, {_origin = DEFAULT_ORIGIN})

        return ('The %s %q is now considered to be managed by the ' ..
                'configuration.\nNote that it may still retain implicit ' ..
                'privileges granted manually or inherited earlier. ' ..
                'Please review its privileges if necessary.')
                :format(user_or_role, name)
    end
end

if #config_entries == 0 then
    return 'Found no users/roles created from the YAML configuration.',
        'Assuming that the users/roles are managed in the old-fashion way',
        '(from Lua).',
        'No further actions are expected.'
end

if #duplicate_entries == 0 and #orphan_entries == 0 then
    return 'Found no users/roles created from Lua. Everything is managed ' ..
        'using the YAML configuration. Fine.'
end

-- The output is a return value, because print() is not visible to
-- the caller in case of a remote console connection.
local output = {}

-- Shortcut function: output a string.
local function o(v)
    table.insert(output, v)
end

-- Output a list.
local function l(vs)
    for _, v in ipairs(vs) do
        o(v)
    end
end

o('Found users/roles that are managed using the YAML configuration')
o('')
l(config_entries)

if #duplicate_entries > 0 then
    -- Add these functions only if needed.
    box.schema.user.disown = gen_disown_f('user')
    box.schema.role.disown = gen_disown_f('role')

    o('')
    o('Some of them will be kept even if removed from the YAML configuration.')
    o('This is a security risk. Use the following commands to transfer the')
    o('ownership to the configuration.')
    o('')
    l(duplicate_entries)
end

if #orphan_entries > 0 then
    o('')
    o('The following users/roles are NOT managed by the YAML configuration.')
    o('It is recommended to add them to the configuration or remove (if they')
    o('are obsoleted). The following commands remove them.')
    o('')
    l(orphan_entries)
end

return output
