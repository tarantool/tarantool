local log = require('internal.config.utils.log')
local utils_file = require('internal.config.utils.file')
local expression = require('internal.config.utils.expression')

local fail_if_vars = {
    tarantool_version = _TARANTOOL:match('^%d+%.%d+%.%d+'),
}
assert(fail_if_vars.tarantool_version ~= nil)

local app_state = {
    -- This variable is used to track the app loaded before the box.cfg() call.
    -- It might be a string with the file name or module name or true if no
    -- app was loaded before the box.cfg() call.
    early_loaded = nil,
}

local function run(config, opts)
    local configdata = config._configdata
    local file = configdata:get('app.file', {use_default = true})
    local module = configdata:get('app.module', {use_default = true})

    if opts == nil then
        opts = {}
    end

    if file ~= nil then
        assert(module == nil)

        local metadata = {}
        local ok, res = pcall(utils_file.get_file_metadata, file)
        if not ok then
            log.error(('Unable to get metadata for file %q: %s'):format(
                file, res))
        else
            metadata = res
        end

        if metadata['fail_if'] ~= nil then
            local expr = metadata['fail_if']
            local ok, res = pcall(expression.eval, expr, fail_if_vars)

            if not ok then
                error(('App %q has invalid "fail_if" expression: %s')
                    :format(file, res), 0)
            end

            if res then
                error(('App %q failed the "fail_if" check: %q')
                    :format(file, expr), 0)
            end
        end

        if metadata['early_load']
        and app_state.early_loaded ~= nil
        and app_state.early_loaded ~= file then
            log.error(('App %q with the "early_load" tag was added ' ..
                       'to the config, it cannot be loaded before the ' ..
                       'first box.cfg call'):format(file))
        end

        if opts.early_load_only and not metadata['early_load'] then
            return
        end

        local fn = assert(loadfile(file))
        log.verbose('app.run: loading '..file)
        fn(file)

        if metadata['early_load'] and app_state.early_loaded == nil then
            app_state.early_loaded = file
        end
    elseif module ~= nil then
        local metadata = {}
        local ok, res = pcall(utils_file.get_module_metadata, module)
        if not ok then
            log.error(('Unable to get metadata for module %q: %s'):format(
                module, res))
        else
            metadata = res
        end

        if metadata['fail_if'] ~= nil then
            local expr = metadata['fail_if']
            local ok, res = pcall(expression.eval, expr, fail_if_vars)

            if not ok then
                error(('App %q has invalid "fail_if" expression: %s')
                    :format(module, res), 0)
            end

            if res then
                error(('App %q failed the "fail_if" check: %q')
                    :format(module, expr), 0)
            end
        end

        if metadata['early_load']
        and app_state.early_loaded ~= nil
        and app_state.early_loaded ~= module
        and package.loaded[module] == nil then
            log.error(('App %q with the "early_load" tag was added ' ..
                       'to the config, it cannot be loaded before the ' ..
                       'first box.cfg call'):format(module))
        end

        if opts.early_load_only and not metadata['early_load'] then
            return
        end

        log.verbose('app.run: loading '..module)
        require(module)

        if metadata['early_load'] and app_state.early_loaded == nil then
            app_state.early_loaded = module
        end
    end
end

-- This apply function will be called before the box_cfg applier.
local function preload(config)
    run(config, {early_load_only = true})
    if app_state.early_loaded == nil then
        -- This was the only run() call before box.cfg() call, lets mark it.
        app_state.early_loaded = true
    end
end

-- This post_apply function will be called after the box_cfg applier.
local function post_apply(config)
    run(config, {early_load_only = false})
end

return {
    stage_1 = {
        name = 'app.stage_1',
        apply = preload,
    },
    stage_2 = {
        name = 'app.stage_2',
        apply = function() end,
        post_apply = post_apply,
    }
}
