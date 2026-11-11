local fio = require('fio')
local t = require('luatest')
local emmylua = require('test.app-luatest.emmylua')

local g = t.group('emmylua')
local root = emmylua.source_root()

-- The test is parameterized by modules. To cover another annotated module,
-- add a spec and a fixture directory with module.lua and object.lua.
local modules = {
    {
        name = 'varbinary',
        source = 'src/lua/varbinary.lua',
        fixtures = 'test/app-luatest/emmylua/varbinary',
        type_name = 'varbinary',
        fields = {
            is = 'Check that the given object',
            new = 'Create a new',
        },
        object = {
            type_name = 'varbinary_object',
            fields = {
                __eq = 'Checks the equality',
                __len = 'Returns the length',
                __tostring = 'Returns the binary data',
            },
        },
    },
    {
        name = 'socket',
        source = 'src/lua/socket.lua',
        fixtures = 'test/app-luatest/emmylua/socket',
        type_name = 'socket',
        fields = {
            tcp_connect = 'Connect a socket to a remote host',
            getaddrinfo = 'useful for finding information',
            tcp_server = 'makes Tarantool act as a server',
        },
        object = {
            type_name = 'socket_object',
            fields = {
                send = 'Send data over a connected socket',
                recv = 'Read `size` bytes',
                close = 'Close (destroy) a socket',
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

        local object_path, object_text =
            open_fixture(client, workspace, 'object.lua')
        local object_uri = emmylua.file_uri(object_path)

        local object_hover = client:request_until('textDocument/hover', {
            textDocument = {uri = object_uri},
            position = emmylua.marker_position(object_text, 'object', -1),
        }, function(result)
            local value = hover_text(result)
            if value == '' or value == 'any' then
                return false
            end
            return has_type_or_fields(
                value, spec.object.type_name, spec.object.fields)
        end, 30)
        local object_type = normalize(hover_text(object_hover))
        t.assert(has_type_or_fields(
            object_type, spec.object.type_name, spec.object.fields),
            ('%s: object type is not inferred: %s'):format(
                spec.name, object_type))

        local object_items = client:request_until(
            'textDocument/completion', {
                textDocument = {uri = object_uri},
                position = emmylua.marker_position(
                    object_text, 'object_fields'),
            }, function(result)
                return has_any_label(result, spec.object.fields)
            end, 30)
        assert_fields(client, object_items, spec.object.fields,
            spec.name .. ' object')
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
