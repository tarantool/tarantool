local fio = require('fio')
local log = require('log')
local yaml = require('yaml')
local file = require('internal.config.utils.file')
local cluster_config = require('internal.config.cluster_config')

local methods = {}
local mt = {
    __index = methods,
}

local function read_config_file(path)
    local data = file.universal_read(path, 'config file')

    -- Integrity module is available only in Tarantool Enterprise Edition
    -- builds.
    local ok, integrity = pcall(require, 'integrity')
    if ok and not integrity.verify_file(path, data) then
        local err = 'Integrity check failed for configuration file %q'
        error(err:format(path))
    end

    local res
    ok, res = pcall(yaml.decode, data)
    if not ok then
        error(('Unable to parse config file %q as YAML: %s'):format(
            path, res))
    end

    -- YAML returns `nil` or `box.NULL` on empty file,
    -- while config sources should be {} if empty.
    if res == nil then
        res = {}
    end

    return res
end

local function new()
    return setmetatable({
        name = 'file',
        type = 'cluster',
        _values = {},
    }, mt)
end

function methods.sync(self, config_module, _iconfig)
    assert(config_module._config_file ~= nil)

    local sources = {config_module._config_file}
    local processed_sources = {}
    local cconfig = {}

    local config
    local i = 1
    while i <= #sources do
        local source = sources[i]

        -- Prevent looping.
        if processed_sources[source] then
            log.warn('skipping already processed config file: %q', source)
            goto continue
        end
        processed_sources[source] = true

        config = read_config_file(source)

        config = cluster_config:apply_conditional(config)

        if config.include ~= nil then
            local insert_idx = 1
            for _, glob in ipairs(config.include) do
                local paths = fio.glob(glob)
                for _, path in ipairs(paths) do
                    path = fio.abspath(path)
                    table.insert(sources, i + insert_idx, path)
                    insert_idx = insert_idx + 1
                end
            end
            config.include = nil
        end

        log.debug('processing config file %q', source)
        cconfig = cluster_config:merge(cconfig, config)

        ::continue::
        i = i + 1
    end

    cluster_config:validate(cconfig)
    self._values = cconfig
end

function methods.get(self)
    return self._values
end

return {
    new = new,
}
