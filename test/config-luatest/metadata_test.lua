local fio = require('fio')
local file = require('internal.config.utils.file')

local t = require('luatest')

---@class luatest.group
local tg = t.group()

local success_cases = {
    {
        name = 'no metadata',
        source = [[-- This module does not contain any metadata
]],
        expected = {},
    },

    {
        name = 'empty metadata',
        source = [[-- This module contains empty metadata
-- --- #tarantool.metadata.v1
-- ...
--
]],
        expected = {},
    },

    {
        name = 'simple metadata',
        source = [[-- This module contains simple metadata
-- --- #tarantool.metadata.v1
-- name: my_module
-- ...
--
]],
        expected = {
            name = 'my_module',
        },
    },

    {
        name = 'complex metadata',
        source = [[-- This module contains complex metadata
-- --- #tarantool.metadata.v1
-- name: my_module
-- foo: true
-- bar:
--   baz: 42
-- qux:
--  - a
--  - b
--  - c
-- ...
--
]],
        expected = {
            name = 'my_module',
            foo = true,
            bar = {
                baz = 42,
            },
            qux = {'a', 'b', 'c'},
        },
    },

    {
        name = 'invalid metadata',
        source = [[-- This module contains invalid metadata tag
-- --- #wrong_tag.metadata.v1
-- name: my_module
-- ...
--
]],
        expected = {},
    },

    {
        name = 'no document end tag',
        source = [[-- This module does not contain document end tag
-- --- #tarantool.metadata.v1
-- name: my_module
--
]],
        expected = {
            name = 'my_module',
        },
    },

    {
        name = 'minimal metadata',
        source = [[-- --- #tarantool.metadata.v1
-- name: my_module]],
        expected = {
            name = 'my_module',
        },
    },

    {
        name = 'multiple metadata tags',
        source = [[-- This module contains multiple metadata tags
-- --- #tarantool.metadata.v1
-- name: my_module
-- ...
--
-- --- #tarantool.metadata.v1
-- name: will_be_ignored
-- ...
--
]],
        expected = {
            name = 'my_module',
        },
    },

    {
        name = 'different metadata version',
        source = [[-- This module contains different metadata version
-- --- #tarantool.metadata.v100
-- name: my_module
-- ...
--
]],
        expected = {
            name = 'my_module',
        },
    },
}

local error_cases = {
    {
        name = 'invalid metadata',
        source = [[-- This module contains invalid metadata
-- --- #tarantool.metadata.v1
-- name: my_module: this is invalid
-- ...
--
]],
        expected = 'Unable to decode YAML metadata',
    },
}

tg.before_each(function()
    tg.origdir = fio.cwd()
    tg.tmpdir = fio.tempdir()
    fio.mktree(tg.tmpdir)
    fio.chdir(tg.tmpdir)
end)

tg.after_each(function()
    fio.chdir(tg.origdir)
    fio.rmtree(tg.tmpdir)
    tg.origdir = nil
    tg.tmpdir = nil
end)

local function write_file(path, content)
    local fh = assert(fio.open(path, {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}))
    fh:write(content)
    fh:close()
end

---@class g luatest.group
tg.test_module_metadata = function(g)
    assert(g.tmpdir ~= nil, 'Temporary directory is not set')

    for _, test in ipairs(success_cases) do
        write_file(g.tmpdir .. '/my_module.lua', test.source)
        local metadata = file.get_module_metadata('my_module')
        t.assert_equals(metadata, test.expected)
    end
end

---@class g luatest.group
tg.test_file_metadata = function(g)
    assert(g.tmpdir ~= nil, 'Temporary directory is not set')

    for _, test in ipairs(success_cases) do
        write_file(g.tmpdir .. '/my_file.lua', test.source)
        local metadata = file.get_file_metadata(g.tmpdir .. '/my_file.lua')
        t.assert_equals(metadata, test.expected)
    end
end

---@class g luatest.group
tg.test_metadata_with_errors = function(g)
    assert(g.tmpdir ~= nil, 'Temporary directory is not set')

    for _, test in ipairs(error_cases) do
        write_file(g.tmpdir .. '/my_module.lua', test.source)
        t.assert_error_msg_contains(test.expected,
            file.get_module_metadata, 'my_module')
    end

    for _, test in ipairs(error_cases) do
        write_file(g.tmpdir .. '/my_file.lua', test.source)
        t.assert_error_msg_contains(test.expected,
            file.get_file_metadata, g.tmpdir .. '/my_file.lua')
    end
end
