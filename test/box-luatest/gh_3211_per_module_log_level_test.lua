local server = require('luatest.server')
local t = require('luatest')

local function find_in_log(cg, str, must_be_present)
    t.helpers.retrying({timeout = 0.3, delay = 0.1}, function()
        local found = cg.server:grep_log(str) ~= nil
        t.assert(found == must_be_present)
    end)
end

local g1 = t.group('gh-3211-1')
local g2 = t.group('gh-3211-2', {{cfg_type = 'log_cfg'},
                                 {cfg_type = 'box_cfg'}})
g1.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)
g2.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(cfg_type)
        rawset(_G, 'log_cfg', function(params)
            local function log2box_cfg(log_params)
                local log2box_keys = {
                    ['level']   = 'log_level',
                    ['modules'] = 'log_modules'
                }

                local box_params = {}
                for k, v in pairs(log_params) do
                    box_params[log2box_keys[k]] = v
                end
                return box_params
            end

            if cfg_type == 'log_cfg' then
                require('log').cfg(params)
            elseif cfg_type == 'box_cfg' then
                box.cfg(log2box_cfg(params))
            end
        end)
    end, {cg.params.cfg_type})
end)

local function drop_server(cg)
    -- TODO(gh-8423): Remove this workaround.
    -- If log level is 'debug' during server:drop(), the test may hang on macOS
    -- (see gh-8420).
    cg.server:update_box_cfg({log_level = 'info',
                             log_modules = {tarantool = 'info'}})
    cg.server:drop()
end
g1.after_each(drop_server)
g2.after_each(drop_server)

-- Test log.new{...}
g1.test_log_new = function(cg)
    cg.server:exec(function()
        local log = require('log')
        t.assert_error_msg_content_equals(
            "Illegal parameters, name should be a string", log.new)
        t.assert_error_msg_content_equals(
            "Illegal parameters, name should be a string", log.new, 123)

        local log1 = log.new('module1')
        local log2 = log.new('module2')
        log.info('info from the default log')
        log1.info('info from the first module')
        log2.info('info from the second module')
    end)

    find_in_log(cg, 'info from the default log', true)
    find_in_log(cg, 'module1 I> info from the first module', true)
    find_in_log(cg, 'module2 I> info from the second module', true)
end

-- Test log.cfg{modules = {...}} and box.cfg{log_modules = {...}}
g2.test_per_module_log_level = function(cg)
    cg.server:exec(function()
        local log = require('log')
        local log_cfg = _G.log_cfg

        local function assert_log_and_box_cfg_equals()
            t.assert_equals(log.cfg.level, box.cfg.log_level)
            t.assert_equals(log.cfg.modules.mod1, box.cfg.log_modules.mod1)
            t.assert_equals(log.cfg.modules.mod2, box.cfg.log_modules.mod2)
            t.assert_equals(log.cfg.modules.mod3, box.cfg.log_modules.mod3)
            t.assert_equals(log.cfg.modules.mod4, box.cfg.log_modules.mod4)
        end

        t.assert_error_msg_contains(
           "modules': should be of type table",
            log_cfg, {modules = 'hello'})
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'log_modules.mod1': " ..
            "should be one of types number, string",
            log_cfg, {modules = {mod1 = {}}})
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'log_modules.mod2': " ..
            "expected crit,warn,info,debug,syserror,verbose,fatal,error",
            log_cfg, {modules = {mod2 = 'hello'}})
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'module name': should be of type string",
            log_cfg, {modules = {[123] = 'debug'}})

        t.assert_equals(log.cfg.modules, nil)
        t.assert_equals(box.cfg.log_modules, nil)
        log_cfg{level = 'info', modules = {mod1 = 'debug',
                                           ['mod2'] = 2,
                                           mod3 = 'error'}}
        -- Check that log.cfg{} without 'modules' options doesn't affect
        -- per-module levels.
        log_cfg{level = 'warn'}
        t.assert_equals(log.cfg.level, 'warn')
        t.assert_equals(log.cfg.modules.mod1, 'debug')
        t.assert_equals(log.cfg.modules.mod2, 2)
        t.assert_equals(log.cfg.modules.mod3, 'error')
        assert_log_and_box_cfg_equals()
        --[[ TODO(gh-7962)
        -- Check that log.cfg{modules = {...}} with the new modules appends them
        -- to the existing ones.
        log_cfg{modules = {mod4 = 4}}
        t.assert_equals(log.cfg.modules.mod1, 'debug')
        t.assert_equals(log.cfg.modules.mod2, 2)
        t.assert_equals(log.cfg.modules.mod3, 'error')
        t.assert_equals(log.cfg.modules.mod4, 4)
        assert_log_and_box_cfg_equals()
        -- Check that log.cfg{modules = {...}} sets levels for the specified
        -- modules, and doesn't affect other modules.
        log_cfg{modules = {mod1 = 0, mod3 = 'info'}}
        t.assert_equals(log.cfg.modules.mod1, 0)
        t.assert_equals(log.cfg.modules.mod2, 2)
        t.assert_equals(log.cfg.modules.mod3, 'info')
        t.assert_equals(log.cfg.modules.mod4, 4)
        assert_log_and_box_cfg_equals()
        -- Check that box.NULL and the empty string remove the corresponding
        -- module from the config.
        log_cfg{modules = {mod2 = box.NULL, mod4 = ''}}
        t.assert_equals(log.cfg.modules.mod1, 0)
        t.assert_equals(log.cfg.modules.mod2, nil)
        t.assert_equals(log.cfg.modules.mod3, 'info')
        t.assert_equals(log.cfg.modules.mod4, nil)
        assert_log_and_box_cfg_equals()
        -- Check that log.cfg{modules = box.NULL} removes all modules.
        log_cfg{modules = box.NULL}
        t.assert_equals(log.cfg.modules, nil)
        t.assert_equals(box.cfg.log_modules, nil)
        --]]
        -- Check that log levels actually affect what is printed to the log.
        log_cfg{level = 'info', modules = {module2 = 4,
                                           module3 = 'debug'}}
        for n = 1, 3 do
            local logn = log.new('module' .. n)
            logn.warn('warn from ' .. logn.name)
            logn.info('info from ' .. logn.name)
            logn.debug('debug from ' .. logn.name)
        end
    end)

    -- module1 has default log level ('info')
    find_in_log(cg, 'warn from module1', true)
    find_in_log(cg, 'info from module1', true)
    find_in_log(cg, 'debug from module1', false)
    -- module2 log level is 4 ('warn')
    find_in_log(cg, 'warn from module2', true)
    find_in_log(cg, 'info from module2', false)
    find_in_log(cg, 'debug from module2', false)
    -- module3 log level is 'debug'
    find_in_log(cg, 'warn from module3', true)
    find_in_log(cg, 'info from module3', true)
    find_in_log(cg, 'debug from module3', true)
end

-- Test automatic module name deduction.
g1.test_modname_deduction = function(cg)
    -- Check that consequent calls to require('log') return the same logger.
    t.assert(require('log') == require('log'))

    cg.server:exec(function()
        local module = require('test.box-luatest.gh_3211_module.testmod')
        module.say_hello()
    end)
    find_in_log(cg, 'testmod I> hello', true)
end

-- Test log.cfg{modules = {tarantool = ...}} and
-- box.cfg{log_modules = {tarantool = ...}} (gh-8320).
g2.test_tarantool_module = function(cg)
    -- When making changes to this test, keep in mind that server logs are
    -- preserved between exec() calls.
    cg.server:exec(function()
        local log = require('log')
        local ffi = require('ffi')
        _G.log_cfg{level = 'debug', modules = {tarantool = 'verbose'}}

        log.debug('Lua debug message 1')
        ffi.C._say(ffi.C.S_DEBUG, nil, 0, nil, 'C debug message 1')
        box.schema.create_space('C_log_level_is_verbose')
    end)
    find_in_log(cg, 'Lua debug message 1', true)
    find_in_log(cg, 'C debug message 1', false)
    find_in_log(cg, 'tuple_new', false) -- printed during create_space()

    cg.server:exec(function()
        local log = require('log')
        local ffi = require('ffi')
        _G.log_cfg{level = 'verbose', modules = {tarantool = 'debug'}}

        log.debug('Lua debug message 2')
        ffi.C._say(ffi.C.S_DEBUG, nil, 0, nil, 'C debug message 2')
        box.schema.create_space('C_log_level_is_debug')
    end)
    find_in_log(cg, 'Lua debug message 2', false)
    find_in_log(cg, 'C debug message 2', true)
    find_in_log(cg, 'tuple_new', true) -- printed during create_space()

    -- Reset global log level to the default value ("info") and check
    -- that Tarantool C log level is still "debug".
    cg.server:exec(function()
        local log = require('log')
        local ffi = require('ffi')
        _G.log_cfg{level = box.NULL}

        log.verbose('Lua verbose message 3')
        ffi.C._say(ffi.C.S_DEBUG, nil, 0, nil, 'C debug message 3')
    end)
    find_in_log(cg, 'Lua verbose message 3', false)
    find_in_log(cg, 'C debug message 3', true)

    -- TODO(gh-7962)
    -- Check that box.NULL unsets custom Tarantool C log level.
    -- _G.log_cfg{modules = {tarantool = box.NULL}}
end
