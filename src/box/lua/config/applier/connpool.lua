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

local function post_apply(_config)
    -- require() it here to avoid a circular dependency.
    local connpool = require('experimental.connpool')

    -- The URIs are reloaded here and not in apply(), because
    -- config:instance_uri() returns URIs of the new configuration
    -- only after all the appliers are done.
    connpool._reload_uris()

    log.verbose('connpool.post_apply: reloaded instance URIs')
end

return {
    name = 'connpool',
    apply = apply,
    post_apply = post_apply,
}
