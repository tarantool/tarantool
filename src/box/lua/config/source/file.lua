local ffi = require('ffi')
local buffer = require('buffer')
local fio = require('fio')
local yaml = require('yaml')

local values = {}

-- Read the file block by block till EOF.
local function stream_read(fh)
    local block_size = 1024
    local buf = buffer.ibuf(block_size)
    while true do
        local rc, err = fh:read(buf:reserve(block_size), block_size)
        if err ~= nil then
            buf:recycle()
            return nil, err
        end
        if rc == 0 then
            -- EOF.
            break
        end
        buf:alloc(rc)
    end
    local res = ffi.string(buf.rpos, buf:size())
    buf:recycle()
    return res
end

-- Read all the file content disregarding whether its size is
-- known from the stat(2) file information.
local function universal_read(file_name, file_kind)
    local fh, err = fio.open(file_name)
    if fh == nil then
        error(('Unable to open %s %q: %s'):format(file_kind, file_name, err))
    end

    -- A FIFO file has zero size in the stat(2) file information.
    --
    -- However, it is useful for manual testing from a shell:
    --
    -- $ tarantool --name <...> --config <(echo '{<...>}')
    --
    -- Let's read such a file block by block till EOF.
    local data
    local err
    if fio.path.is_file(file_name) then
        data, err = fh:read()
    else
        data, err = stream_read(fh)
    end

    fh:close()

    if err ~= nil then
        error(('Unable to read %s %q: %s'):format(file_kind, file_name, err))
    end

    return data
end

local function sync(config_module, _iconfig)
    assert(config_module._config_file ~= nil)

    local data = universal_read(config_module._config_file, 'config file')
    local ok, res = pcall(yaml.decode, data)
    if not ok then
        error(('Unable to parse config file %q as YAML: %s'):format(
            config_module._config_file, res))
    end

    values = res
end

local function get()
    return values
end

local function meta()
    return nil
end

return {
    name = 'file',
    -- The type is either 'instance' or 'cluster'.
    type = 'cluster',
    -- Gather most actual config values.
    sync = sync,
    -- Access the configuration after source.sync().
    --
    -- source.get()
    get = get,
    -- Metadata of the current configuration from the source.
    meta = meta,
}
