local console = require('console')
local log = require('internal.config.utils.log')
local instance_config = require('internal.config.instance_config')
local fio = require('fio')

-- Active socket and its location.
local listening_socket = nil
local listening_socket_abspath = nil

local function socket_file_to_listen_uri(file)
    if file:startswith('/') or file:startswith('./') then
        return ('unix/:%s'):format(file)
    end
    return ('unix/:./%s'):format(file)
end

local function close_listening_socket()
    if listening_socket ~= nil then
        listening_socket:close()
        listening_socket = nil
        listening_socket_abspath = nil
    end
end

local function apply(config)
    local configdata = config._configdata
    local enabled = configdata:get('console.enabled', {use_default = true})
    if not enabled then
        log.debug('console.apply: console is disabled by the ' ..
            'console.enabled option; skipping...')
        -- If the console is disabled, we must close the active socket.
        close_listening_socket()
        return
    end

    local new_socket_file = configdata:get('console.socket',
        {use_default = true})
    assert(new_socket_file ~= nil)

    -- The same socket file is pointed by different paths before
    -- and after first box.cfg() if the `process.work_dir` option
    -- is set.
    local iconfig_def = configdata._iconfig_def
    new_socket_file = instance_config:prepare_file_path(iconfig_def,
        new_socket_file)
    local new_socket_abspath = fio.abspath(new_socket_file)

    -- If console.socket has changed we must close the listening socket.
    if listening_socket_abspath ~= new_socket_abspath then
        close_listening_socket()
    end

    if listening_socket_abspath == nil then
        local listen_uri = socket_file_to_listen_uri(new_socket_file)
        log.debug('console.apply: %s', listen_uri)
        listening_socket = console.listen(listen_uri)
        listening_socket_abspath = new_socket_abspath
    end
end

return {
    name = 'console',
    apply = apply,
}
