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
        name = 'net_box',
        source = 'src/box/lua/net_box.lua',
        fixtures = 'test/app-luatest/emmylua/net_box',
        type_name = 'net.box',
        fields = {
            connect = 'Create a connection to a remote instance',
            new = 'Create a connection (compatibility alias)',
            from_fd = 'Create a connection from a file descriptor',
            timeout = 'a default connection timeout',
            self = 'the connection object for the local Tarantool server',
        },
        objects = {
            {
                fixture = 'conn.lua',
                type_name = 'net.box.conn',
                fields = {
                    host = 'the remote host',
                    port = 'the remote port',
                    state = 'the current connection state',
                    error = 'the error message',
                    protocol = 'the IPROTO protocol version',
                    schema_version = 'the remote schema version',
                    peer_uuid = "the peer's UUID",
                },
            },
            {
                fixture = 'space.lua',
                type_name = 'net.box.space',
                fields = {
                    id = 'the space id',
                    name = 'the space name',
                    engine = 'the storage engine of the space',
                    field_count = 'the number of fields in the space',
                    enabled = 'whether the space is enabled',
                    temporary = 'whether the space is temporary',
                    is_sync = 'whether the space is synchronous',
                    index = 'the space indexes',
                    connection = 'the connection the space belongs to',
                },
            },
            {
                fixture = 'index.lua',
                type_name = 'net.box.index',
                fields = {
                    space = 'the space the index belongs to',
                    id = 'the index id',
                    name = 'the index name',
                    type = 'the index type',
                    parts = 'the index parts',
                    unique = 'whether the index is unique',
                },
            },
            {
                fixture = 'stream.lua',
                type_name = 'net.box.stream',
                fields = {
                    space = 'the stream spaces',
                },
            },
            {
                fixture = 'watcher.lua',
                type_name = 'net.box.watcher',
                fields = {
                    unregister = 'unregister the watcher',
                },
            },
            {
                fixture = 'future.lua',
                type_name = 'net.box.future',
                fields = {
                    is_ready = 'is_ready',
                    result = 'result',
                    wait_result = 'wait_result',
                    discard = 'discard',
                    pairs = 'pairs',
                },
            },
            {
                fixture = 'self.lua',
                type_name = 'net.box.self',
                fields = {
                    ping = 'ping',
                    reload_schema = 'reload_schema',
                    close = 'close',
                    timeout = 'timeout',
                    wait_connected = 'wait_connected',
                    is_connected = 'is_connected',
                    call = 'call',
                    eval = 'eval',
                },
            },
            {
                fixture = 'connect_options.lua',
                type_name = 'net.box.connect_options',
                fields = {
                    user = 'a username used to connect',
                    password = "a user's password",
                    wait_connected = 'a connection timeout',
                    reconnect_after = 'a number of seconds to wait',
                    connect_timeout = 'a number of seconds to wait',
                    fetch_schema = 'whether to fetch schema changes',
                },
            },
            {
                fixture = 'request_options.lua',
                type_name = 'net.box.request_options',
                fields = {
                    is_async = 'whether the request is asynchronous',
                    iterator = 'iterator',
                    limit = 'limit',
                    offset = 'offset',
                    on_push = 'callback for each inbound message',
                },
            },
            {
                fixture = 'call_options.lua',
                type_name = 'net.box.call_options',
                fields = {
                    is_async = 'whether the request is asynchronous',
                    on_push = 'callback for each inbound message',
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
