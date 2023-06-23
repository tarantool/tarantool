local errno = require('errno')
local console = require('console')
local log = require('internal.config.utils.log')

local function socket_file_to_listen_uri(file)
    if file:startswith('/') or file:startswith('./') then
        return ('unix/:%s'):format(file)
    end
    return ('unix/:./%s'):format(file)
end

local function apply(config)
    local configdata = config._configdata
    local enabled = configdata:get('console.enabled', {use_default = true})
    if not enabled then
        log.debug('console.apply: console is disabled by the ' ..
            'console.enabled option; skipping...')
        return
    end

    local socket_file = configdata:get('console.socket', {use_default = true})
    assert(socket_file ~= nil)

    local listen_uri = socket_file_to_listen_uri(socket_file)
    log.debug('console.apply: %s', listen_uri)

    -- Ignore 'Address already in use' error, because it is
    -- natural effect of reloading the configuration. All others
    -- errors are re-raised and lead to failure of apply of the
    -- configuration.
    local ok, res = pcall(console.listen, listen_uri)
    if not ok and errno() ~= errno.EADDRINUSE then
        error(res)
    end
end

return {
    name = 'console',
    apply = apply,
}
