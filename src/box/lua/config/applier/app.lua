local log = require('internal.config.utils.log')

local function apply(_config)
    log.verbose('app.apply: do nothing')
end

local function post_apply(config)
    local configdata = config._configdata
    local file = configdata:get('app.file', {use_default = true})
    local module = configdata:get('app.module', {use_default = true})
    if file ~= nil then
        assert(module == nil)
        local fn = assert(loadfile(file))
        log.verbose('app.post_apply: loading '..file)
        fn(file)
    elseif module ~= nil then
        log.verbose('app.post_apply: loading '..module)
        require(module)
    end
end

return {
    name = 'app',
    apply = apply,
    post_apply = post_apply,
}
