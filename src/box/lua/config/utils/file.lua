local ffi = require('ffi')
local buffer = require('buffer')
local fio = require('fio')
local yaml = require('yaml')

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
        error(('Unable to open %s %q: %s'):format(file_kind, file_name, err), 0)
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
        error(('Unable to read %s %q: %s'):format(file_kind, file_name, err), 0)
    end

    return data
end


local function get_file_tags(file_name)
    local function parse_tags(t)
        local known_tags = {
            early_load = 'boolean',
        }

        local tags = {}
        for k, v in pairs(t) do
            if known_tags[k] == type(v) then
                tags[k] = v
            end
        end

        return tags
    end

    -- Tags are only supported for Lua files.
    if type(file_name) ~= 'string' or not file_name:endswith('.lua') then
        error(('Invalid file name %q'):format(file_name), 0)
    end

    local file, err = io.open(file_name, "r")
    if file == nil then
        error(('Unable to open %q: %s'):format(file_name, err), 0)
    end

    local tags = {}
    local line = file:read("*line")

    -- Skip shebang.
    if line:startswith('#!') then
        line = file:read("*line")
    end

    -- Skip empty lines.
    while line:lstrip() == '' do
        line = file:read("*line")
    end

    -- Trim leading whitespaces.
    line = string.lstrip(line)

    if line:startswith('--') then
        local ok, res = pcall(yaml.decode, line:sub(3))
        if ok and type(res) == 'table' then
            tags = parse_tags(res)
        end
    end

    -- @TODO Add support for multi-line comments.

    file:close()
    return tags
end

local function get_module_tags(module_name)
    local ok, res = pcall(package.search, module_name)
    if not ok then
        error(('Unable to locate package %q: %s'):format(module_name, res), 0)
    end

    if res == nil then
        error(('Unable to locate package %q'):format(module_name), 0)
    end

    return get_file_tags(res)
end

return {
    universal_read = universal_read,
    get_file_tags = get_file_tags,
    get_module_tags = get_module_tags,
}
