local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'dflt'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks uncovered function definition error conditions.
g.test_func_def_err_conditions = function(cg)
    cg.server:exec(function()
        local function setmap(table)
            return setmetatable(table, { __serialize = 'map' })
        end

        local date = os.date("%Y-%m-%d %H:%M:%S")

        t.assert_error_msg_equals("Tuple field 3 (name) type does not " ..
                                  "match one required by operation: " ..
                                  "expected string, got nil", function()
            box.space._func:auto_increment{box.session.euid(), nil, 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 5 (language) type does not " ..
                                  "match one required by operation: " ..
                                  "expected string, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, nil,
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 17 (comment) type does not " ..
                                  "match one required by operation: " ..
                                  "expected string, got unsigned", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, 777, date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 7 (routine_type) type does " ..
                                  "not match one required by operation: " ..
                                  "expected string, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', nil, {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 11 (sql_data_access) type " ..
                                  "does not match one required by operation:" ..
                                  " expected string, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           nil, false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 14 (is_null_call) type does " ..
                                  "not match one required by operation: " ..
                                  "expected boolean, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, nil, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 1 (id) type does not match " ..
                                  "one required by operation: " ..
                                  "expected unsigned, got nil", function()
            box.space._func:insert{nil, 777, box.session.euid(), 'f', 0, 'lua',
                                   '', 'function', {}, 'any', 'none',
                                   'none', false, false, true, {}, setmap{}, '',
                                   date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 2 (owner) type does not " ..
                                  "match one required by operation: " ..
                                  "expected unsigned, got nil", function()
            box.space._func:auto_increment{nil, 'f', 0, 'lua', '', 'function',
                                           {}, 'any', 'none', 'none', false,
                                           false, true, {}, setmap{}, '', date,
                                           date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 4 (setuid) type does not " ..
                                  "match one required by operation: " ..
                                  "expected unsigned, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', nil, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 13 (is_sandboxed) type does " ..
                                  "not match one required by operation: " ..
                                  "expected boolean, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, nil, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 9 (returns) type does not " ..
                                  "match one required by operation: " ..
                                  "expected string, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, nil, 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 15 (exports) type does not " ..
                                  "match one required by operation: " ..
                                  "expected array, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, nil,
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 10 (aggregate) type does not " ..
                                  "match one required by operation: " ..
                                  "expected string, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', nil,
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 8 (param_list) type does not " ..
                                  "match one required by operation: " ..
                                  "expected array, got nil", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', nil, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 8 type does not match one " ..
                                  "required by operation: expected string, " ..
                                  "got unsigned", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function',
                                           {777}, 'any',
                                           'none', 'none', false, false, true,
                                           {}, setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Tuple field 20 (trigger) type does not " ..
                                  "match one required by operation: " ..
                                  "expected array, got unsigned", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, 10}
        end)
        t.assert_error_msg_equals("Tuple field 20 type does not match one " ..
                                  "required by operation: expected string, " ..
                                  "got unsigned", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           setmap{}, '', date, date, {10}}
        end)
        t.assert_error_msg_equals("Failed to create function 'f': " ..
                                  "invalid argument type", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function',
                                           {'wrong argument type'}, 'any',
                                           'none', 'none', false, false, true,
                                           {}, setmap{}, '', date, date, {}}
        end)
        t.assert_error_msg_equals("Wrong function options: "..
                                  "'is_multikey' must be boolean", function()
            box.space._func:auto_increment{box.session.euid(), 'f', 0, 'lua',
                                           '', 'function', {}, 'any', 'none',
                                           'none', false, false, true, {},
                                           {is_multikey = 'wrong type'}, '',
                                           date, date, {}}
        end)
    end)
end
