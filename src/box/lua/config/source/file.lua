local fio = require('fio')
local log = require('internal.config.utils.log')
local yaml = require('yaml')
local file = require('internal.config.utils.file')
local cluster_config = require('internal.config.cluster_config')

local stack_methods = {}
local Stack = {
    __index = stack_methods,
}

local function stack_selfcheck(self, method_name)
    if type(self) ~= 'table' or getmetatable(self) ~= Stack then
        local fmt_str = 'Use Stack:%s(<...>) instead of Stack.%s(<...>)'
        error(fmt_str:format(method_name, method_name), 0)
    end
end

local function new_stack(table)
    if table == nil then
        table = {}
    end

    if type(table) ~= 'table' then
        error('Expected table, got ' .. type(table))
    end

    return setmetatable({
        _stack = table,
        _count = #table,
    }, Stack)
end

function stack_methods:push(value)
    stack_selfcheck(self, 'push')

    if value == nil then
        return
    end

    self._count = self._count + 1
    self._stack[self._count] = value
end

function stack_methods:pop()
    if self._count == 0 then
        return nil
    end

    local value = self._stack[self._count]
    self._stack[self._count] = nil
    self._count = self._count - 1

    return value
end

function stack_methods:iterator()
    return self.pop, self
end

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

    local config_paths = new_stack({config_module._config_file})
    local processed_paths = {}
    local cconfig = {}

    -- Do a DFS traversal using stack.
    local config
    for config_path in config_paths:iterator() do
        -- Prevent recursion.
        if processed_paths[config_path] then
            log.warn('skipping already processed config file: %q',
                     config_path)
            goto continue
        end
        processed_paths[config_path] = true

        log.debug('processing config file %q', config_path)
        config = read_config_file(config_path)

        -- The contract is that we must validate the config before doing
        -- anything with it.
        cluster_config:validate(config)
        config = cluster_config:apply_conditional(config)

        if config.include ~= nil then
            local current_config_dir = fio.dirname(config_path)

            -- Because we need to preserve the order of the includes, create a
            -- new stack for new paths, where we will push all new config files
            -- found in `include` section. This stack will contain them in
            -- reverse order, so we will be able to push them to the
            -- config_paths stack in the correct order.
            local new_config_paths = new_stack()

            for _, glob in ipairs(config.include) do
                -- Treat relative include paths as relative
                -- to the config file, where they are included.
                glob = file.rebase_file_abspath(current_config_dir, glob)
                local paths = fio.glob(glob)
                for _, path in ipairs(paths) do
                    new_config_paths:push(path)
                end
            end

            for new_config_path in new_config_paths:iterator() do
                config_paths:push(new_config_path)
            end

            config.include = nil
        end

        cconfig = cluster_config:merge(cconfig, config)

        ::continue::
    end

    self._values = cconfig
end

function methods.get(self)
    return self._values
end

return {
    new = new,
}
