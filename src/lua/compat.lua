-- compat.lua -- internal file

local options_format = {
    old     = "boolean",
    new     = "boolean",
    default = "boolean",
    frozen  = "boolean",
    brief   = "string",
    doc     = "string"
}

local options = {
    json_escape_forward_slash = {
        old = true,
        new = false,
        default = true,
        frozen = false,
        brief = "json escapes '/' during encode",
        doc  = "https://github.com/tarantool/tarantool/wiki/compat_json_escape_forward_slash"
    },
    option_2 = {
        old = false,
        new = true,
        frozen = false,
        default = true,
        brief = "<...>",
        doc  = "https://github.com/tarantool/tarantool/wiki/option_2"
    },
    option_3 = {
        old = true,
        new = false,
        frozen = true,
        default = false,
        brief = "<...>",
        doc  = "https://github.com/tarantool/tarantool/wiki/option_3"
    }
}

local postaction = {
    json_escape_forward_slash = function(value)
            require('json').cfg{encode_esc_slash = value}
        end,
    option_2 = function()
            -- print("option_2 postaction was called!")
        end,
    option_3 = function()
            assert(not "option_3 is frozen, its postaction should never be called")
        end
}

local help = [[
This is Tarantool compatibility module.
To get help, see the Tarantool manual at https://tarantool.io/en/doc/
Available commands:

    candidates()                    -- list all unselected options
    dump()                          -- get command that sets up compat with same options as current
    help()                          -- show this help
    reset()                         -- set all options to default
    add_options{{name, {}, action}} -- add new options by providing name, option table and postaction for each one
    restore{'option_name'}          -- set to default specified options
    {option_name = true}            -- set listed options to desired values (true, false, 'old', 'new', 'default')
    option_name                     -- list option info
    option_name = true              -- set desired value to option, could be true, false, 'old', 'new', 'default'
]]

local cfg = { }

local function serialize_policy(key, policy)
    assert(policy ~= nil)
    local result = { }
    for f in pairs(policy) do
        result[f] = policy[f]
    end
    result.value = cfg[key].value
    return result
end

local function serialize_compat()                       -- should it list frozen options?
    local result = { }
    for key, val in pairs(options) do
        result[key] = serialize_policy(key, val)
    end
    return result
end

local function verify_option(name, option)
    for p, t in pairs(options_format) do
        if type(option[p]) ~= t then
            error(('Invalid option table for %s, bad %s (%s is expected)'):format(name, p, t))
        end
        if option.frozen and not option.default == option.new then
            error(('Frozen option %s default is wrong'):format(name))
        end
    end
end

local function set_option(key, val)
    if not options[key] then
        error(('Invalid option %s'):format(key))
    end
    if val == 'new' then
        val = options[key].new
    end
    if val == 'old' then
        val = options[key].old
    end
    local default = false;
    if val == 'default' then
        val = options[key].default
        default = true
    end
    if type(val) ~= 'boolean' then
        error(('Invalid argument %s'):format(val))
    end
    if not options[key].frozen then
        if options[key].default == options[key].new and val == options[key].old then
            print("WARNING: chosen option provides outdated behavior and will soon get frozen!")
            print(("For more info, see the Tarantool manual at %s"):format(options[key].doc))
        end
        postaction[key](val)
    else if not default then
        if val == options[key].new then
            print("WARNING: chosen option is the only available!")      -- a better way to throw warnings?
        else
            error("Chosen option is no longer available")               -- is it a good idea to throw errors this way,
                                                                        -- so file and line get listed?
        end
    end end
    cfg[key].value = val
    cfg[key].selected = true
end

local compat = setmetatable({
            candidates = function()
                local result = { }
                for key, val in pairs(options) do
                    if not cfg[key].selected and not options[key].frozen then
                        result[key] = serialize_policy(key, val)
                    end
                end
                return result
            end,
            dump = function()                                           -- should frozen options be listed in dump?
                local result = "require('tarantool').compat({"
                local isFirst = true
                for key, _ in pairs(options) do
                    if cfg[key].selected then
                        if not isFirst then
                            result = result .. ", "
                        end
                        result = result .. key ..  " = " .. tostring(cfg[key].value)
                        isFirst = false
                    end
                end
                return result .. "})"
            end,
            reset = function()
                for key, _ in pairs(options) do
                    set_option(key, 'default')
                    cfg[key].selected = false
                end
            end,
            restore = function(list)
                if type(list) ~= 'table' then
                    error(('Invalid argument %s'):format(list))
                end
                for _, key in pairs(list) do
                    set_option(key, 'default')
                    cfg[key].selected = false
                end
            end,
            add_options = function(list)
                if type(list) ~= 'table' then
                    error(('Invalid argument %s'):format(list))
                end
                for _, val in pairs(list) do
                    local name, option, action = unpack(val)
                    verify_option(name, option)
                    options[name] = option
                    postaction[name] = action
                    cfg[name] = {value = option.default, selected = false}
                end
            end,
            preload = function()
                for key, elem in pairs(options) do
                    verify_option(key, elem)
                    cfg[key] = {value = elem.default, selected = false}
                end
            end,
            postload = function()
                for key, _ in pairs(options) do
                    if not options[key].frozen then
                        postaction[key](cfg[key].value)
                    end
                end
            end,
            help = function()
                print()                                             -- not sure about this
                print(help)
            end
        }, {
            __call = function(_, list)
                if type(list) ~= 'table' then
                    error(('Invalid argument %s'):format(list))
                end
                for key, val in pairs(list) do
                    set_option(key, val)
                end
            end,
            __newindex = function(_, key, val)
                set_option(key, val)
            end,
            __index = function(_, key)
                local policy = options[key]
                if not policy then
                    error(('Invalid option %s'):format(key))
                end
                return serialize_policy(key, policy);
            end,
            __serialize = serialize_compat,
            __tostring  = serialize_compat,
            __autocomplete = function(self)
                local res = { }
                for key, _ in pairs(options) do
                    if not options[key].frozen then
                        res[key] = true
                    end
                end
                return res
            end
        }
    )

compat.preload()
compat.preload = nil
compat.postload()               -- when should we perform postload?
compat.postload = nil
                                -- "A log warning at start for old values. Possibly also CLI / UI warnings." ?
return compat
