local t = require('luatest')
local compat = require('compat')

local g = t.group()

local NEW = 'new'
local OLD = 'old'
local DEFAULT = 'default'

local reset = function(compat)
    local f = loadstring(compat.dump('default'))
    f()
end

local option_1_called = false
local option_2_called = false

local hot_reload_option_called

local definitions = {
    option_1_option_def = {
        name = 'option_1',
        default = 'new',
        brief = 'option_1',
        action = function()
            option_1_called = true
        end,
        run_action_now = true
    },
    option_2_option_def = {
        name = 'option_2',
        default = 'old',
        brief = 'option_2',
        obsolete = nil,         -- Explicitly mark option as non-obsolete.
        action = function()
            option_2_called = true
        end,
        run_action_now = false
  },
}

g.before_all(function()
    reset(compat)
    for _, option_def in pairs(definitions) do
        compat.add_option(option_def)
    end
    option_1_called = false
end)
g.after_all( function() reset(compat) end)


local obsolete_option_def = {
    name = 'obsolete_option',
    default = 'new',
    brief = 'obsolete_option',
    obsolete = '5.0'
  }

local test_options_calls = function()
    t.assert(option_1_called)
    t.assert(option_2_called)
    option_1_called = false
    option_2_called = false
end

g.test_add_option = function()
    local local_option_1_called = false
    local local_option_2_called = false
    local local_option_1_def = {
        name = 'local_option_1',
        default = 'old',
        brief = 'local_option_1',
        action = function() local_option_1_called = true end,
        run_action_now = true
    }
    local local_option_2_def = {
        name = 'local_option_2',
        default = 'new',
        brief = 'local_option_2',
        action = function() local_option_2_called = true end
    }
    compat.add_option(local_option_1_def)
    compat.add_option(local_option_2_def)
    t.assert_equals(type(compat.local_option_1), 'table')
    t.assert_equals(type(compat.local_option_2), 'table')
    t.assert(local_option_1_called)
    t.assert_not(local_option_2_called)

    -- Obsolete option with default == 'old'.
    local bad_option_def = {
        name = 'bad_option_1',
        default = 'old',
        obsolete = '5.0',
        brief = 'bad_option',
    }

    t.assert_error(compat.add_option, bad_option_def)

    -- Non obsolete option with bad action.
    bad_option_def = {
        name ='bad_option_2',
        default = false,
        obsolete = nil,
        brief = 'bad_option',
        action = 'not a function'
    }

    t.assert_error(compat.add_option, bad_option_def)

    -- Wrong name type.
    bad_option_def = {
        name = {},
        default = 'new',
        obsolete = nil,
        brief = 'bad_option',
        action = function()
            print('bad_option called!')
        end
    }

    t.assert_error(compat.add_option, bad_option_def)
end

g.test_index = function()
    reset(compat)
    for _, option_def in pairs(definitions) do
        local name = option_def.name
        t.assert(compat[name])
        local option = compat[name]
        t.assert_equals(option.current, DEFAULT)
        t.assert_equals(option.default, option_def.default)
        t.assert_equals(option.brief, option_def.brief)
        if option.obsolete then
            t.assert(option_def.brief)
        end
    end
end

g.test_new_index = function()
    for _, option_def in pairs(definitions) do
        local name = option_def.name
        t.assert(compat[name])
        if not compat[name].obsolete then
            compat[name] = 'default'
            compat[name] = 'old'
            compat[name] = 'new'
        end
        t.assert_equals(compat[name].current, NEW)
    end
    test_options_calls()
    t.assert_error(getmetatable(compat).__newindex, compat,
                   'option_1', 'invalid_value')
    t.assert_error(getmetatable(compat).__newindex, compat,
                   'no_such_option', 'old')
end

g.test_obsolete = function()
    compat.add_option(obsolete_option_def)
    t.assert_error(getmetatable(compat).__newindex, compat,
                   'obsolete_option', 'old')
    compat.obsolete_option = 'new'
end

-- There should be no obsolete options in definitions.
g.test_call = function()
    local arg = { }
    for _, option_def in pairs(definitions) do
        arg[option_def.name] = 'old'
    end
    compat(arg)
    for _, option_def in pairs(definitions) do
        t.assert_equals(compat[option_def.name].current, OLD)
    end
    test_options_calls()
end

g.test_serialize = function()
    reset(compat)
    local serialized = getmetatable(compat).__serialize(compat)

    local prev_pos = 0
    local combined = {}
    for pos, option in pairs(serialized) do
        t.assert_equals(pos, prev_pos + 1)
        prev_pos = pos

        -- option is a table with a single key-value pair.
        local name, value = next(option)
        combined[name] = value
    end

    local exp = { }
    for _, option_def in pairs(definitions) do
        exp[option_def.name] = ('default (%s)'):format(option_def.default)
    end
    t.assert_items_include(combined, exp)
end

g.test_dump = function()
    reset(compat)
    assert(compat.option_1.current == DEFAULT)
    assert(loadstring(compat.dump()))()
    t.assert_equals(compat.option_1.current, DEFAULT)
    assert(loadstring(compat.dump('new')))()
    t.assert_equals(compat.option_1.current, NEW)
    assert(loadstring(compat.dump('old')))()
    t.assert_equals(compat.option_1.current, OLD)
    assert(loadstring(compat.dump('default')))()
    t.assert_equals(compat.option_1.current, DEFAULT)
    assert(loadstring(compat.dump('current')))()
    t.assert_equals(compat.option_1.current, NEW)
    reset(compat)
end

g.test_help = function()
    t.assert(compat.help())
end

g.test_is_new_is_old = function()
    for _, option_def in pairs(definitions) do
        local name = option_def.name
        t.assert(compat[name])

        compat[name] = 'new'
        t.assert(compat[name]:is_new())
        t.assert_not(compat[name]:is_old())

        compat[name] = 'old'
        t.assert_not(compat[name]:is_new())
        t.assert(compat[name]:is_old())

        compat[name] = 'default'
        local is_new = option_def.default == 'new'
        t.assert_equals(compat[name]:is_new(), is_new)
        t.assert_equals(compat[name]:is_old(), not is_new)

        t.assert_error_msg_content_equals(
            'usage: compat.<option_name>:is_new()',
            compat[name].is_new)
        t.assert_error_msg_content_equals(
            'usage: compat.<option_name>:is_old()',
            compat[name].is_old)
    end
end

g.test_hot_reload = function()
    local hot_reload_option_def_1 = {
        name = 'hot_reload_option',
        default = 'new',
        brief = 'hot_reload_option_1',
        obsolete = nil,
        action = function()
            hot_reload_option_called = true
        end,
        run_action_now = true
    }

    compat.add_option(hot_reload_option_def_1)
    compat.hot_reload_option = 'new'
    t.assert_equals(compat.hot_reload_option.current, NEW)
    t.assert(hot_reload_option_called)
    hot_reload_option_called = false

    local hot_reload_option_def_2 = {
        name = 'hot_reload_option',
        default = 'old',
        brief = 'hot_reload_option_2',
        obsolete = nil,
        action = function()
            hot_reload_option_called = true
        end
        -- run_action_now == false by default.
    }

    compat.add_option(hot_reload_option_def_2)
    t.assert_equals(compat.hot_reload_option.current, NEW)
    t.assert_equals(compat.hot_reload_option.brief, 'hot_reload_option_2')
    t.assert_not(hot_reload_option_called)
end

-- Autocomplete tests.

local console = require('console')

local function tabcomplete(s)
    return console.completion_handler(s, 0, #s)
end

g.test_autocomplete = function()
    rawset(_G, 'compat', compat)

    local exp = {
        'compat.',
        'compat.dump(',
        'compat.help(',
        'compat.add_option(',
    }
    local cnt = #exp
    for _, option in pairs(getmetatable(compat).__serialize(compat)) do
        -- option is a table with a single key-value pair.
        local name = next(option)
        if not compat[name].obsolete then
            cnt = cnt + 1
            exp[cnt] = 'compat.' .. name
        end
    end

    t.assert_items_equals(tabcomplete('compat.'), exp)
end
