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
        name = 'iconv',
        source = 'src/lua/iconv.lua',
        fixtures = 'test/app-luatest/emmylua/iconv',
        type_name = 'iconv',
        fields = {
            new = 'Construct a new',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'iconv.converter',
                fields = {},
                forbidden_fields = {'__tostring'},
                description = 'converter object that converts a string',
            },
        },
    },
    {
        name = 'tap',
        source = 'src/lua/tap.lua',
        fixtures = 'test/app-luatest/emmylua/tap',
        type_name = 'tap',
        fields = {
            test = 'Initialize a test',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'taptest',
                forbidden_types = {'boolean'},
                fields = {
                    strict = 'Flag, true if comparisons',
                    test = 'Create a subtest',
                    plan = 'Indicate how many tests',
                    check = 'Checks the number of tests',
                    diag = 'Display a diagnostic message',
                    ok = 'This is a basic function',
                    fail = 'is equivalent to',
                    skip = 'is equivalent to',
                    is = 'Check whether the first argument equals',
                    ['isnt'] = 'This is the negation of',
                    is_deeply = 'Recursive version of',
                    like = 'Verify a string against a pattern',
                    unlike = 'This is the negation of',
                    isnil = 'Test whether a value is nil',
                    isnumber = 'Test whether a value is a number',
                    isstring = 'Test whether a value is a string',
                    istable = 'Test whether a value is a table',
                    isboolean = 'Test whether a value is a boolean',
                    isfunction = 'Test whether a value is a function',
                    isudata = 'Test whether a value is a userdata',
                    iscdata = 'Test whether a value is a cdata',
                },
            },
        },
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
                fields = {},
                forbidden_fields = {'__tostring'},
                description = 'variable-length binary object',
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
        name = 'decimal',
        source = 'src/lua/decimal_stub.lua',
        fixtures = 'test/app-luatest/emmylua/decimal',
        fields = {
            abs = 'Returns absolute value of a decimal number',
            exp = 'Returns e raised to the power of a decimal number',
            is_decimal = 'if the specified value is a decimal',
            ln = 'Returns natural logarithm of a decimal number',
            log10 = 'Returns base-10 logarithm of a decimal number',
            new = 'Returns the value of the input as a decimal number',
            precision = 'Returns the number of digits in a decimal number',
            rescale = 'Returns the number after possible rounding or padding',
            scale = 'Returns the number of post-decimal digits',
            sqrt = 'Returns the square root of a decimal number',
            trim = 'Returns a decimal number after possible removing',
            round = 'Rounds a decimal number to a specified number of',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'decimal',
                fields = {},
                forbidden_fields = {'__tostring'},
                description = 'A decimal number',
            },
        },
    },
    {
        name = 'pickle',
        source = 'src/lua/pickle_stub.lua',
        fixtures = 'test/app-luatest/emmylua/pickle',
        fields = {
            pack = 'Convert Lua variables to binary format',
            unpack = 'Convert Lua variables back from binary format',
        },
        objects = {},
    },
    {
        name = 'console',
        source = 'src/box/lua/console.lua',
        fixtures = 'test/app-luatest/emmylua/console',
        fields = {
            set_default_output = 'Set default output format',
            get_default_output = 'Get default output format',
            eos = 'end-of-output string',
            delimiter = 'Set a custom end-of-request marker',
            initial_env = 'initial console environment',
            set_initial_env = 'Set initial console environment',
            eval = 'Evaluate a line of Lua code',
            ac = 'Set the auto-completion flag',
            connect = 'Connect to the instance',
            listen = 'Listen for incoming requests',
        },
        objects = {},
    },
    {
        name = 'crypto',
        source = 'src/lua/crypto.lua',
        fixtures = 'test/app-luatest/emmylua/crypto',
        type_name = 'crypto_module',
        fields = {
            digest = 'Digest algorithms',
            hmac = 'HMAC algorithms',
            cipher = 'Cipher algorithms',
            cipher_algo = 'Cipher algorithm identifiers',
            cipher_mode = 'Cipher mode identifiers',
        },
        objects = {
            {
                fixture = 'digest.lua',
                type_name = 'crypto.digest',
                fields = {
                    init = 'Initialize the digest context',
                    update = 'Feed data',
                    result = 'Return the digest value',
                    free = 'Free the digest context',
                },
            },
            {
                fixture = 'hmac.lua',
                type_name = 'crypto.hmac',
                fields = {
                    init = 'Initialize the HMAC',
                    update = 'Feed data',
                    result = 'Return the HMAC value',
                    free = 'Free the HMAC context',
                },
            },
            {
                fixture = 'cipher.lua',
                type_name = 'crypto.cipher',
                fields = {
                    init = 'Initialize',
                    update = 'Process data',
                    result = 'Return final chunk',
                    free = 'Free the cipher context',
                },
            },
        },
    },
    {
        name = 'uuid',
        source = 'src/lua/uuid.lua',
        fixtures = 'test/app-luatest/emmylua/uuid',
        fields = {
            NULL = 'A nil UUID object',
            new = 'Create a UUID',
            fromstr = 'Create a UUID from a 36-byte hexadecimal string',
            frombin = 'Create a UUID from a 16-byte binary string',
            bin = 'Create a UUID and return its 16-byte binary form',
            str = 'Create a UUID and return its 36-byte hexadecimal form',
            is_uuid = 'Check if the specified value has the UUID type',
        },
        objects = {
            {
                fixture = 'object.lua',
                type_name = 'uuid',
                fields = {
                    bin = 'Return the UUID as a 16-byte binary string',
                    str = 'Return the UUID as a 36-byte hexadecimal string',
                    isnil = 'Check if the UUID is an all-zero value',
                },
                forbidden_fields = {'__tostring'},
                description = 'A UUID object',
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

local function assert_forbidden_fields(items, names, context)
    local labels = {}
    for _, item in ipairs(items) do
        labels[item.label] = true
    end

    for _, name in ipairs(names) do
        t.assert(labels[name] == nil,
            ('%s: completion has %s'):format(context, name))
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
            for _, forbidden in ipairs(object.forbidden_types or {}) do
                -- Only the inferred type in the header is checked, not the
                -- field list in the hover body.
                local header = object_type:match('^([^{]*)') or object_type
                t.assert(not header:find(forbidden, 1, true),
                    ('%s: object type contains %s: %s'):format(
                        object.type_name, forbidden, header))
            end
            if object.description ~= nil then
                t.assert(
                    object_type:find(object.description, 1, true) ~= nil,
                    ('%s: hover has no description %q: %s'):format(
                        object.type_name, object.description,
                        object_type))
            end

            local object_items = nil
            if next(object.fields) ~= nil then
                object_items = client:request_until(
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
            if object.forbidden_fields ~= nil then
                if object_items == nil then
                    -- The object may have no members of its own: the
                    -- conversion hooks (such as `__tostring`) belong to
                    -- the metatable and are not exposed as fields. The
                    -- hover check above has already warmed up the
                    -- analysis, so a single request is enough here.
                    object_items = client:request(
                        'textDocument/completion', {
                            textDocument = {uri = object_uri},
                            position = emmylua.marker_position(
                                object_text, 'object_fields'),
                        }, 30)
                    if type(object_items) ~= 'table' then
                        object_items = {}
                    end
                end
                assert_forbidden_fields(object_items,
                    object.forbidden_fields, object.type_name)
            end
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
