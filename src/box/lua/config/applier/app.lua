local log = require('internal.config.utils.log')
local utils_file = require('internal.config.utils.file')

local function run(config, opts)
    local configdata = config._configdata
    local file = configdata:get('app.file', {use_default = true})
    local module = configdata:get('app.module', {use_default = true})

    if opts == nil then
        opts = {}
    end

    if file ~= nil then
        assert(module == nil)

        if opts.preload_only then
            local tags = {}
            local ok, res = pcall(utils_file.get_file_tags, file)
            if not ok then
                log.error(('Unable to get tags for file %q: %s'):format(
                    file, res))
            else
                tags = res
            end

            if not tags['preload'] then
                return
            end
        end

        local fn = assert(loadfile(file))
        log.verbose('app.run: loading '..file)
        fn(file)
    elseif module ~= nil then
        if opts.preload_only then
            local tags = {}
            local ok, res = pcall(utils_file.get_module_tags, module)
            if not ok then
                log.error(('Unable to get tags for module %q: %s'):format(
                    module, res))
            else
                tags = res
            end

            if not tags['preload'] then
                return
            end
        end

        log.verbose('app.run: loading '..module)
        require(module)
    end
end

-- This apply function will be called before the box.cfg is applied.
local function apply(config)
    run(config, {preload_only = true})
end

-- This post_apply function will be called after the box.cfg is applied.
local function post_apply(config)
    run(config, {preload_only = false})
end

return {
    stage_1 = {
        name = 'app.stage_1',
        apply = apply,
    },
    stage_2 = {
        name = 'app.stage_2',
        apply = function() end,
        post_apply = post_apply,
    }
}
