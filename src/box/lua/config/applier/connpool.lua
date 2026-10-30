local log = require('internal.config.utils.log')

local function apply(config)
    -- require() it here to avoid a circular dependency.
    local connpool = require('experimental.connpool')

    local configdata = config._configdata
    local idle_timeout = configdata:get('connpool.idle_timeout',
                                        {use_default = true})

    connpool.set_idle_timeout(idle_timeout)

    log.verbose(('connpool.apply: set idle timeout to %d'):format(idle_timeout))
end

return {
    name = 'connpool',
    apply = apply,
}
