local ffi = require('ffi')
local buffer = require('buffer')
local fio = require('fio')
local yaml = require('yaml')
local loaders = require('internal.loaders')

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

-- Interpret the given path as relative to the given base
-- directory. And return the absolute path.
local function rebase_file_abspath(base_dir, path)
    -- fio.pathjoin('/foo', '/bar') gives '/foo/bar/', see
    -- gh-8816.
    --
    -- Let's check whether the second path is absolute.
    local needs_prepending = base_dir ~= nil and not path:startswith('/')
    if needs_prepending then
        path = fio.pathjoin(base_dir, path)
    end

    -- Let's consider the following example.
    --
    -- config:
    --   context:
    --     foo:
    --       from: file
    --       file: ../foo.txt
    -- process:
    --   work_dir: x
    --
    -- In such a case we get correct "x/../foo.txt" on
    -- startup, but fio.open() complains if there is no "x"
    -- directory.
    --
    -- The working directory is created at startup if needed,
    -- so a user is not supposed to create it manually before
    -- first tarantool startup.
    --
    -- Let's strip all these ".." path components using
    -- fio.abspath().
    path = fio.abspath(path)

    return path
end

-- Interpret the given path as relative to the given base
-- directory. If the resulting path is inside CWD, then
-- return a relative path, otherwise return an absolute.
local function rebase_file_path(base_dir, path)
    path = rebase_file_abspath(base_dir, path)

    -- If possible, use a relative path. Sometimes an
    -- absolute path overruns the Unix socket path length limit
    -- (107 on Linux and 103 on Mac OS -- in our case).
    --
    -- Using a relative path allows to listen on the given Unix
    -- socket path in such cases.
    local cwd = fio.cwd()
    if cwd == nil then
        return path
    end
    if not cwd:endswith('/') then
        cwd = cwd .. '/'
    end
    if path:startswith(cwd) then
        path = path:sub(#cwd + 1, #path)
    end
    return path
end

local function get_file_metadata(file_name)
    local function escape_regex(str)
        return str:gsub('([%.%+%-%*%?%[%]%(%)%$%^%{%}%|%<%>%~])', '%%%1')
    end

    -- Tags are only supported for Lua files.
    if type(file_name) ~= 'string' or not file_name:endswith('.lua') then
        error(('Invalid file name %q'):format(file_name), 0)
    end

    local file, err = io.open(file_name, "r")
    if file == nil then
        error(('Unable to open %q: %s'):format(file_name, err), 0)
    end

    local metadata = {}

    local line_count = 0
    local yaml_string = ''
    local comment_tag = nil
    -- Currently the only supported version is 1 and its value is not used.
    -- luacheck: ignore 311 value assigned to a local variable is unused.
    local metadata_version = nil
    local in_comment, in_yaml = false, false
    while true do
        local line = file:read("*line")
        if line == nil then
            break
        end
        line_count = line_count + 1

        -- Skip shebang.
        if line_count == 1 and line:startswith('#!') then
            goto next_line
        end

        -- Skip empty lines.
        line = line:lstrip()
        if line == '' then
            goto next_line
        end

        if not in_comment and line:startswith('--') then
            in_comment = true
        end

        if in_comment then
            if not line:startswith('--') then
                break
            end
        end

        if in_comment and not in_yaml then
            comment_tag, metadata_version = line:match(
                '^(%-%-[%s]+)%-%-%- #tarantool.metadata.v([%d]+)[%s]*$')
            metadata_version = tonumber(metadata_version)

            if comment_tag ~= nil and metadata_version ~= nil then
                comment_tag = escape_regex(comment_tag)
                in_yaml = true
            end
        end

        if in_yaml then
            line = line:match(('^%s(.*)'):format(comment_tag))

            if line == nil then
                break
            end

            yaml_string = yaml_string .. '\n' .. line

            if line:startswith('...') then
                break
            end
        end

        ::next_line::
    end
    file:close()

    if yaml_string ~= '' then
        local ok, res = pcall(yaml.decode, yaml_string)
        if not ok then
            error(('Unable to decode YAML metadata in %q: %s'):format(
                file_name, res), 0)
        elseif type(res) == 'table' then
            metadata = res
        end
    end
    -- @TODO Add support for multi-line comments

    return metadata
end

local function get_module_metadata(module_name)
    -- Builtin modules have no metadata.
    if loaders.builtin[module_name] ~= nil then
        return {}
    end

    local ok, res = pcall(package.search, module_name)
    if not ok then
        error(('Unable to locate package %q: %s'):format(module_name, res), 0)
    end

    if res == nil then
        error(('Unable to locate package %q'):format(module_name), 0)
    end

    return get_file_metadata(res)
end

return {
    universal_read = universal_read,
    rebase_file_abspath = rebase_file_abspath,
    rebase_file_path = rebase_file_path,
    get_file_metadata = get_file_metadata,
    get_module_metadata = get_module_metadata,
}
