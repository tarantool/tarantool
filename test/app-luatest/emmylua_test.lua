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

local function assert_global(client, spec, workspace)
    local path, text = open_fixture(
        client, workspace, spec.global.fixture)
    local uri = emmylua.file_uri(path)
    local values = {}
    for marker in pairs(spec.global.hovers) do
        local result = client:request_until('textDocument/hover', {
            textDocument = {uri = uri},
            position = emmylua.marker_position(text, marker, -1),
        }, function(res)
            local value = normalize(hover_text(res))
            return value ~= '' and value ~= 'any' and
                not value:find('unknown', 1, true)
        end, 30)
        values[marker] = normalize(hover_text(result))
    end
    for marker, needles in pairs(spec.global.hovers) do
        for _, needle in ipairs(needles) do
            t.assert_str_contains(values[marker], needle, false,
                ('%s global %s: missing %q in %s'):format(
                    spec.name, marker, needle, values[marker]))
        end
    end
    local items = client:request('textDocument/completion', {
        textDocument = {uri = uri},
        position = emmylua.marker_position(text, 'fields', 0),
    }, 10)
    local labels = {}
    for _, item in ipairs(items) do
        labels[item.label] = true
    end
    for _, name in ipairs(spec.global.completions) do
        t.assert(labels[name] == true,
            ('%s global completion has no %s'):format(spec.name, name))
    end
    if spec.fields ~= nil then
        assert_fields(client, items, spec.fields, spec.name .. ' global')
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

local string_global_spec = {
    name = 'string',
    source = 'src/lua/string_stub.lua',
    fixtures = 'test/app-luatest/emmylua/string',
    fields = {
        split = 'Split a string into parts',
        ljust = 'Left-justify a string',
        rjust = 'Right-justify a string',
        center = 'Center a string',
        startswith = 'Check for a prefix',
        endswith = 'Check for a suffix',
        hex = 'Return hex representation',
        fromhex = 'Decode hex digits',
        strip = 'Strip both ends',
        lstrip = 'Strip the left side',
        rstrip = 'Strip the right side',
    },
    global = {
        fixture = 'global.lua',
        hovers = {
            split = {'string[]'},
            ljust = {'string'},
            rjust = {'string'},
            center = {'string'},
            startswith = {'boolean'},
            endswith = {'boolean'},
            hex = {'string'},
            fromhex = {'string'},
            strip = {'string'},
            lstrip = {'string'},
            rstrip = {'string'},
        },
        completions = {
            'split',
            'ljust',
            'rjust',
            'center',
            'startswith',
            'endswith',
            'hex',
            'fromhex',
            'strip',
            'lstrip',
            'rstrip',
        },
    },
}

local function run_global_test(spec)
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
        assert_global(client, spec, workspace)
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

for _, spec in ipairs({string_global_spec}) do
    g['test_global_' .. spec.name] = function()
        run_global_test(spec)
    end
end
