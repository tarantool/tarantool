local fio = require('fio')
local t = require('luatest')
local emmylua = require('test.app-luatest.emmylua')

local g = t.group('emmylua')
local root = emmylua.source_root()

-- The test is parameterized by modules. To cover another annotated module,
-- add a spec and a fixture directory with module.lua and object.lua.
local modules = {
    {
        name = 'strict',
        source = 'src/lua/strict.lua',
        fixtures = 'test/app-luatest/emmylua/strict',
        type_name = 'strict',
        fields = {
            on = 'Enable strict mode',
            off = 'Disable strict mode',
        },
        objects = {},
    },
    {
        name = 'varbinary',
        source = 'src/lua/varbinary.lua',
        fixtures = 'test/app-luatest/emmylua/varbinary',
        type_name = 'varbinary',
        fields = {
            is = 'Check that the given object',
            new = 'Create a new',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'varbinary_object',
                fields = {
                    __tostring = 'Returns the binary data',
                },
            },
        },
    },
    {
        name = 'xlog',
        source = 'src/box/lua/xlog.lua',
        fixtures = 'test/app-luatest/emmylua/xlog',
        type_name = 'xlog',
        fields = {
            pairs = 'Open a file, and allow iterating',
            meta = 'Open an xlog file',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'xlog.meta',
                fields = {
                    filetype = 'The file type',
                    instance_uuid = 'The instance UUID',
                    prev_vclock = 'The previous vclock',
                },
            },
            {
                fixture = 'entry_header.lua',
                type_name = 'xlog.entry_header',
                fields = {
                    type = 'The request type',
                    lsn = 'The log sequence number',
                    replica_id = 'The identifier of the replica',
                    timestamp = 'The request timestamp',
                },
            },
        },
    },
    {
        name = 'fiber',
        source = 'src/lua/fiber.lua',
        fixtures = 'test/app-luatest/emmylua/fiber',
        type_name = 'fiber',
        fields = {
            create = 'Create and start a fiber',
            new = 'Create a fiber but do not start it',
            sleep = 'sleep for the specified number of seconds',
            yield = 'Yield control to the scheduler',
            status = 'Return the status of the current fiber',
            info = 'Return information about all fibers',
            top = 'Show all alive fibers',
            kill = 'Cancel a fiber',
            time = 'Get the current system time',
            clock = 'Get the monotonic time in seconds',
            channel = 'Create a channel',
            cond = 'Create a conditional variable',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'fiber_object',
                fields = {
                    id = "Get a fiber's ID",
                    name = 'Get or set a name',
                    status = "Get a fiber's status",
                    cancel = 'Send a cancellation request',
                    set_max_slice = 'Set max',
                    storage = 'Local storage within the fiber',
                    set_joinable = 'Make joinable',
                    join = 'Join a fiber',
                },
            },
            {
                fixture = 'channel.lua',
                type_name = 'fiber.channel',
                fields = {
                    put = 'Send a message',
                    get = 'Fetch and remove a message',
                    close = 'Close the channel',
                    is_empty = 'Check whether the channel is empty',
                    count = 'Find out how many messages',
                    is_full = 'Check if a channel is full',
                    is_closed = 'Check if a channel is closed',
                },
            },
            {
                fixture = 'cond.lua',
                type_name = 'fiber.cond',
                fields = {
                    wait = 'Wait for a signal',
                    signal = 'Wake up a single fiber',
                    broadcast = 'Wake up all fibers',
                },
            },
            {
                fixture = 'info.lua',
                type_name = 'fiber.info',
                fields = {
                    csw = 'Context switch count',
                    memory = 'memory occupied by the fiber',
                    time = 'duplicates the "time" entry',
                    name = 'name of the fiber',
                    fid = 'id of the fiber',
                    backtrace = "fiber's stack trace",
                },
            },
            {
                fixture = 'top.lua',
                type_name = 'fiber.top',
                fields = {
                    instant = 'the share of',
                    average = 'exponential moving average',
                    time = 'estimates how much CPU',
                },
            },
            {
                fixture = 'slice.lua',
                type_name = 'fiber.slice',
                fields = {
                    warn = 'A warning slice in seconds',
                    err = 'An error slice in seconds',
                },
            },
        },
    },
}

local function normalize(value)
    return (value:gsub('%s+', ' '))
end

local function hover_text(result)
    local contents = result and result.contents
    if type(contents) == 'table' then
        contents = contents.value
    end
    if type(contents) == 'string' then
        return contents
    end
    return ''
end

local function documentation_text(result)
    local documentation = result and result.documentation
    if type(documentation) == 'table' then
        documentation = documentation.value
    end
    if type(documentation) == 'string' then
        return documentation
    end
    return ''
end

local function has_any_label(items, fields)
    if type(items) ~= 'table' then
        return false
    end
    for _, item in ipairs(items) do
        if fields[item.label] ~= nil then
            return true
        end
    end
    return false
end

local function has_type_or_fields(value, type_name, fields)
    if type_name ~= nil and value:find(type_name, 1, true) ~= nil then
        return true
    end
    for field in pairs(fields) do
        if value:find(field, 1, true) ~= nil then
            return true
        end
    end
    return false
end

local function copy_fixtures(workspace, spec)
    local dir = fio.pathjoin(root, spec.fixtures)
    local names = assert(fio.listdir(dir))
    for _, name in ipairs(names) do
        local path = fio.pathjoin(dir, name)
        if fio.path.is_file(path) then
            emmylua.copy_file(path, fio.pathjoin(workspace, name))
        end
    end
    emmylua.copy_file(
        fio.pathjoin(root, 'test/app-luatest/emmylua/.emmyrc.json'),
        fio.pathjoin(workspace, '.emmyrc.json'))
end

local function open_fixture(client, workspace, name)
    local path = fio.pathjoin(workspace, name)
    local text = emmylua.read_file(path)
    client:did_open(path, text)
    return path, text
end

local function assert_fields(client, items, fields, context)
    local by_label = {}
    for _, item in ipairs(items) do
        if by_label[item.label] == nil then
            by_label[item.label] = item
        end
    end

    for field, phrase in pairs(fields) do
        local item = by_label[field]
        t.assert(item ~= nil,
            ('%s: completion has no %s'):format(context, field))
        local resolved = client:request('completionItem/resolve', item)
        local documentation = normalize(documentation_text(resolved))
        t.assert_str_contains(documentation, phrase, false,
            ('%s: %s has no description'):format(context, field))
    end
end

local function run_module_test(spec)
    local binary = os.getenv('EMMYLUA_LS')
    t.skip_if(binary == nil or binary == '',
        'EMMYLUA_LS is not set')

    local workspace = fio.tempdir()
    local resources = fio.pathjoin(workspace, 'resources')
    local client = nil
    local ok, err = xpcall(function()
        emmylua.copy_file(
            fio.pathjoin(root, spec.source),
            fio.pathjoin(workspace, spec.name .. '.lua'))
        copy_fixtures(workspace, spec)

        client = emmylua.new_client({
            binary = binary,
            workspace = workspace,
            resources = resources,
        })
        client:initialize()

        local module_path, module_text =
            open_fixture(client, workspace, 'module.lua')
        local module_uri = emmylua.file_uri(module_path)

        local hover = client:request_until('textDocument/hover', {
            textDocument = {uri = module_uri},
            position = emmylua.marker_position(module_text, 'module', -1),
        }, function(result)
            local value = hover_text(result)
            if value == '' or value == 'any' then
                return false
            end
            return has_type_or_fields(value, spec.type_name, spec.fields)
        end, 30)
        local module_type = normalize(hover_text(hover))
        t.assert(module_type ~= 'any',
            ('%s: module has no inferred type'):format(spec.name))
        t.assert(has_type_or_fields(module_type, spec.type_name, spec.fields),
            ('%s: module type is not inferred: %s'):format(
                spec.name, module_type))

        local module_items = client:request_until(
            'textDocument/completion', {
                textDocument = {uri = module_uri},
                position = emmylua.marker_position(
                    module_text, 'module_fields'),
            }, function(result)
                return has_any_label(result, spec.fields)
            end, 30)
        assert_fields(client, module_items, spec.fields, spec.name .. ' module')

        for _, object in ipairs(spec.objects) do
            local object_path, object_text =
                open_fixture(client, workspace, object.fixture)
            local object_uri = emmylua.file_uri(object_path)

            local object_hover = client:request_until(
                'textDocument/hover', {
                    textDocument = {uri = object_uri},
                    position = emmylua.marker_position(
                        object_text, 'object', -1),
                }, function(result)
                    local value = hover_text(result)
                    if value == '' or value == 'any' then
                        return false
                    end
                    return has_type_or_fields(
                        value, object.type_name, object.fields)
                end, 30)
            local object_type = normalize(hover_text(object_hover))
            t.assert(has_type_or_fields(
                object_type, object.type_name, object.fields),
                ('%s: object type is not inferred: %s'):format(
                    object.type_name, object_type))

            local object_items = client:request_until(
                'textDocument/completion', {
                    textDocument = {uri = object_uri},
                    position = emmylua.marker_position(
                        object_text, 'object_fields'),
                }, function(result)
                    return has_any_label(result, object.fields)
                end, 30)
            assert_fields(client, object_items, object.fields,
                object.type_name)
        end
    end, debug.traceback)

    if client ~= nil then
        pcall(function()
            client:close()
        end)
    end
    pcall(fio.rmtree, workspace)
    if not ok then
        error(err)
    end
end

for _, spec in ipairs(modules) do
    g['test_' .. spec.name] = function()
        run_module_test(spec)
    end
end
