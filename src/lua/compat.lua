-- compat.lua -- module intended to solve compatibility problems in different
-- parts of Tarantool. Introduced in gh-7000, see also gh-6912.

local NEW = true
local OLD = false

local tweaks = require('internal.tweaks')

local options_format = {
    default        = 'string',
    brief          = 'string',
    obsolete       = 'string/nil',
    run_action_now = 'boolean/nil',
    action         = 'function/nil',
}

local JSON_ESCAPE_BRIEF = [[
Whether to escape the forward slash symbol '/' using a backslash in a
json.encode() result. The old and the new behavior produce a result, which
is compatible ith the JSON specification. However most of other JSON encoders
don't escape the forward slash, so the new behavior is considered more safe.

https://github.com/tarantool/tarantool/wiki/compat%3Ajson_escape_forward_slash
]]

local YAML_PRETTY_MULTILINE_BRIEF = [[
Whether to encode in block scalar style all multiline strings or ones
containing "\n\n" substring. The new behavior makes all multiline string output
as single text block which is handier for the reader, but may be incompatible
with some existing applications that rely on the old style.

https://github.com/tarantool/tarantool/wiki/compat%3Ayaml_pretty_multiline
]]

local FIBER_CHANNEL_GRACEFUL_CLOSE_BRIEF = [[
Whether fiber channel should be marked read-only on close, instead of being
destroyed. The new behavior allows user to mark the channel as read-only. This
way if a message was in the buffer before close, it will remain accessible after
close. The read-only channel will be closed automatically when the buffer
becomes empty, or will be deleted by GC if all links to it are lost, as before.
The new behavior can break code that relies on `ch:get()` returning `nil` after
channel close.

https://github.com/tarantool/tarantool/wiki/compat%3Afiber_channel_close_mode
]]

-- Returns an action callback that toggles a tweak.
local function tweak_action(tweak_name, old_tweak_value, new_tweak_value)
    return function(is_new)
        if is_new then
            tweaks[tweak_name] = new_tweak_value
        else
            tweaks[tweak_name] = old_tweak_value
        end
    end
end

-- Contains options descriptions in following format:
-- * default  (string)
-- * brief    (string)
-- * obsolete (string or nil)
-- * current  (boolean, true for 'new')
-- * selected (boolean)
-- * action   (function)
local options = {
    json_escape_forward_slash = {
        default = 'old',
        obsolete = nil,
        brief = JSON_ESCAPE_BRIEF,
        run_action_now = true,
        action = tweak_action('json_escape_forward_slash', true, false),
    },
    yaml_pretty_multiline = {
        default = 'old',
        obsolete = nil,
        brief = YAML_PRETTY_MULTILINE_BRIEF,
        action = tweak_action('yaml_pretty_multiline', false, true),
    },
    fiber_channel_close_mode = {
        default = 'old',
        obsolete = nil,
        brief = FIBER_CHANNEL_GRACEFUL_CLOSE_BRIEF,
        action = tweak_action('fiber_channel_close_mode',
                              'forceful', 'graceful'),
    },
}

-- Array with option names in order of addition.
local options_order = { }

local help = [[
Tarantool compatibility module.
To get help, see the Tarantool manual at https://github.com/tarantool/tarantool/wiki/compat .
Available commands:

    compat.help()                   -- show this help
    compat.<option_name>            -- list option info
    compat.<option_name> = 'old'    -- set desired value to option, could be
                                       'old', 'new', 'default'
    compat{<option_name> = 'new'}   -- set listed options to desired values
                                       ('old', 'new', 'default')
    compat.dump()                   -- get Lua command that sets up different
                                       compat with same options as current
    compat.add_option()             -- add new option by providing a table with
                                       name, default, brief, obsolete and action
                                       function
]]

-- Returns table with all non-obsolete options from `options` and their values.
local function serialize_compat()
    -- The results of serialization should be ordered correctly.
    -- The only feasible way for it is using indexed table with
    -- values {option_name = val}.
    local result = { }

    for _, name in pairs(options_order) do
        local option = options[name]
        if option.selected and option.current == NEW and
                not option.obsolete then
            table.insert(result, {[name] = 'new'})
        end
    end
    for _, name in pairs(options_order) do
        local option = options[name]
        if option.selected and option.current == OLD and
                not option.obsolete then
            table.insert(result, {[name] = 'old'})
        end
    end
    for _, name in pairs(options_order) do
        local option = options[name]
        if not option.selected and not option.obsolete then
            local val = option.current and 'new' or 'old'
            table.insert(result, {[name] = ('default (%s)'):format(val)})
        end
    end

    -- Result is an indexed table but this way user can interact with it as if
    -- it was key-value.
    result = setmetatable(result, {
        __index = function(self, key)
            for i = 1, #self do
                if self[i][key] ~= nil then
                    return self[i][key]
                end
            end
        end
    })

    return result
end

-- Checks options correctness.
local function verify_option(name, option)
    if type(name) ~= 'string' then
        local msg = 'Option name must be a string (%s provided)'
        error(msg:format(type(name)))
    end
    for p, t in pairs(options_format) do
        if not t:find(type(option[p])) then
            local msg = 'Invalid option table for %s, bad %s (%s is expected)'
            error(msg:format(name, p, t))
        end
    end
    if option.default ~= 'new' and option.default ~= 'old' then
        local msg = "Invalid option table for %s, bad default" ..
                    " ('new'/'old' is expected)"
        error(msg:format(name))
    end
    if not option.obsolete and option.action == nil then
        local msg = "Invalid option table for %s, bad action" ..
                    " (function is expected)"
        error(msg:format(name))
    end
    if option.obsolete and option.default == 'old' then
        error(('Obsolete option %s default is wrong.'):format(name))
    end
end

-- Checks if operation is valid, sets value to an option and runs postaction.
local function set_option(name, val)
    local option = options[name]
    if not option then
        error(('Invalid option %s'):format(name))
    end
    local selected
    if val == 'new' then
        val = NEW
        selected = true
    elseif val == 'old' then
        val = OLD
        selected = true
    elseif val == 'default' then
        val = option.default == 'new'
        selected = false
    else
        error(('Invalid argument %s in option %s'):format(val, name))
    end
    if option.obsolete and val == OLD then
        error(('Chosen option %s is no longer available'):format(name))
    end
    if val ~= option.current then
        option.action(val)
    end
    option.current = val
    option.selected = selected
end

local compat = { }

function compat.dump(mode)
    -- dump() works in one of the following modes:
    -- dump compat configuration as is with `default` if any
    local ACT_NIL     = 0
    -- dump compat configuration as is but replace `default` with
    -- its value (new/old)
    local ACT_CURRENT = 1
    -- dump everything as `new`
    local ACT_NEW     = 2
    -- dump everything as `old`, besides obsolete, that could be only `new`
    local ACT_OLD     = 3
    -- dump everything as `default`
    local ACT_DEFAULT = 4
    local action
    if mode == 'new' then
        action = ACT_NEW
    elseif mode == 'old' then
        action = ACT_OLD
    elseif mode == 'default' then
        action = ACT_DEFAULT
    elseif mode == 'current' then
        action = ACT_CURRENT
    elseif mode == nil then
        action = ACT_NIL
    else
        error("usage: compat.dump('new'/'old'/'default'/'current'/nil)")
    end
    local result = [[require('compat')({]]
    local max_key_len = 0
    for _, key in pairs(options_order) do
        max_key_len = (#key > max_key_len) and #key or max_key_len
    end
    for _, key in pairs(options_order) do
        local val
        local comment
        if options[key].obsolete then
            if action == ACT_NEW or action == ACT_OLD or
                    action == ACT_CURRENT or
                    (action == ACT_NIL and options[key].selected) then
                val = "'new'"
            else
                val = "'default'"
            end
            comment = 'obsolete since ' .. options[key].obsolete
        else
            if action == ACT_CURRENT then
                if options[key].selected and options[key].current == NEW or
                        options[key].default == 'new' then
                    val = "'new'"
                else
                    val = "'old'"
                end
            elseif action == ACT_DEFAULT or (action == ACT_NIL and
                    not options[key].selected) then
                val = "'default'"
            elseif action == ACT_NEW or
                    (action == ACT_NIL and options[key].current == NEW) then
                val = "'new'"
            else
                val = "'old'"
            end
        end
        -- Can't use '\t' due to gh-7681.
        local form = '%s\n    %s%s = %s,'
        result = form:format(result, key,
                             string.rep(' ', max_key_len - #key), val)
        if comment then
            if (val ~= "'default'") then
                -- For alignment.
                result = result .. '    '
            end
            result = result .. ' -- ' .. comment
        end
    end
    if not result:find('\n') then
        return result .. '})'
    else
        -- Need double '\n' due to gh-3012.
        return result .. '\n})\n\n'
    end
end

-- option_def:
-- * name           (string)
-- * default        ('new'/'old')
-- * brief          (string)
-- * obsolete       (string/nil)
-- * action         (function/nil)
-- * run_action_now (boolean/nil)
function compat.add_option(option_def)
    if type(option_def) ~= 'table' then
        error("usage: compat.add_option({name = '...', default = 'new'/'old'" ..
              ", brief = '...', action = func, run_action_now = true/false})")
    end
    local name = option_def.name
    verify_option(name, option_def)

    local current = option_def.default == 'new' and NEW or OLD
    -- If not hot reload, set `current` and `selected`.
    if not options[name] then
        options[name] = {
            current = current,
            selected = false
        }
        table.insert(options_order, name)
    -- If hot reload but option is set to 'default', update `current`.
    elseif not options[name].selected then
        options[name].current = current
    end
    -- Else keep `current` and `selected` as is.

    -- Copy all other fields.
    local option = options[name]
    option.brief    = option_def.brief
    option.default  = option_def.default
    option.obsolete = option_def.obsolete
    option.action   = option_def.action

    if not option.obsolete and option_def.run_action_now then
        option.action(options[name].current)
    end
end

function compat.help()
    return help
end

function compat.preload()
    for key, option in pairs(options) do
        verify_option(key, option)
        table.insert(options_order, key)
        option.current = option.default == 'new'
        option.selected = false
    end
    -- Preload should be run only once and is removed not to confuse users.
    compat.preload = nil
end

function compat.postload()
    for _key, option in pairs(options) do
        if not option.obsolete and option.run_action_now then
            option.action(option.current)
        end
        option.run_action_now = nil
    end
    -- Postload should be run only once and is removed not to confuse users.
    compat.postload = nil
end

local compat_mt = { }

function compat_mt.__call(_, list)
    if type(list) ~= 'table' then
        error("usage: compat({<option_name> = 'new'/'old'/'default'})")
    end
    for key, val in pairs(list) do
        set_option(key, val)
    end
end

function compat_mt.__newindex(_, key, val)
    set_option(key, val)
end

function compat_mt.__index(_, key)
    if not options[key] then
        error(('Invalid option %s'):format(key))
    end
    local result = {
        brief = options[key].brief,
        default = options[key].default,
        obsolete = options[key].obsolete,
    }

    if not options[key].selected then
        result.current = 'default'
    elseif options[key].current == NEW then
        result.current = 'new'
    else
        result.current = 'old'
    end

    return result
end

compat_mt.__serialize = serialize_compat

function compat_mt.__tostring()
    -- YAML can't be required at the beginning as it isn't initialized by then.
    return require('yaml').encode(serialize_compat())
end

function compat_mt.__autocomplete()
    local res = { }
    for key, option in pairs(options) do
        if not option.obsolete then
            res[key] = true
        end
    end
    return res
end


-- Preload reads hardcoded into `options` table options, verifies
-- them and sets other compat-specific values.
compat.preload()
-- Postload calls actions of hardcoded options if according
-- `run_action_now` is `true`. Most of the actions call `require`
-- but the required modules are unlikely to be loaded at compat
-- initialization, so it may be needed to move this call somewhere
-- else in the future.
compat.postload()

compat = setmetatable(compat, compat_mt)

return compat
