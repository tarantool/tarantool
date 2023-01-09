#!/usr/bin/env tarantool

local fio = require('fio')

-- {{{ Helpers to determine the script directory

local function sourcefile()
    local info = debug.getinfo(3, 'S')
    local source = info and info.source
    return source and source:startswith('@') and source:sub(2) or nil
end

local function sourcedir()
    local source = sourcefile()
    return fio.abspath(source and source:match('(.*)/') or '.')
end

-- }}} Helpers to determine the script directory

local destdir = sourcedir()

box.cfg({
    -- Place the resulting snapshot near to this fill.lua script.
    work_dir = destdir,
    -- We need only the last snapshot.
    --
    -- The option has default value 2, so if we omit it, then two
    -- snapshots will be stored in the directory.
    checkpoint_count = 1,
    -- Our goal is to generate a snapshot and we don't need WALs.
    wal_mode = 'none',
})

-- Allow the testing code to connect to the instance.
box.schema.user.grant('guest', 'super')

-- Ensure that the snapshot is generated with certain schema
-- version.
--
-- The gh-4691 test needs the schema of the 2.1.3 version.
local schema_version_tuple = box.space._schema:get({'version'})
assert(schema_version_tuple ~= nil)
assert(schema_version_tuple[1] == 'version')
assert(schema_version_tuple[2] == 2)
assert(schema_version_tuple[3] == 1)
assert(schema_version_tuple[4] == 3)

-- Add data necessary for the gh-4691 test.
--
-- It needs a space with an index with a collation.
box.schema.create_space('test')
box.space.test:create_index('pk', {
    parts = {
        {field = 1, type = 'string', collation = 'unicode_ci'}
    },
})

-- Bump the snapshot.
box.snapshot()
local snaps = fio.glob('*.snap')
assert(#snaps == 1)
print('Created ' .. fio.abspath(snaps[1]))

os.exit()
