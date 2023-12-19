local yaml = require('yaml')
local file = require('internal.config.utils.file')

local methods = {}
local mt = {
    __index = methods,
}

function methods.sync(self, config_module, _iconfig)
    assert(config_module._config_file ~= nil)

    local data = file.universal_read(config_module._config_file, 'config file')

    -- Integrity module is available only in Tarantool Enterprise Edition
    -- builds.
    local ok, integrity = pcall(require, 'integrity')
    if ok and not integrity.verify_file(config_module._config_file, data) then
        local err = 'Integrity check failed for configuration file %q'
        error(err:format(config_module._config_file))
    end

    local res
    ok, res = pcall(yaml.decode, data)
    if not ok then
        error(('Unable to parse config file %q as YAML: %s'):format(
            config_module._config_file, res))
    end

    self._values = res
end

function methods.get(self)
    return self._values
end

local function new()
    return setmetatable({
        name = 'file',
        type = 'cluster',
        _values = {},
    }, mt)
end

return {
    new = new,
}
