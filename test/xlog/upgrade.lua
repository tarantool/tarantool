#!/usr/bin/env tarantool

local work_dir = nil
local memtx_dir = nil

if arg[1] ~= nil then
    -- When no args are passed to the script test-run fills args with
    -- "nil upgrade.lua" for some reason.
    work_dir = arg[1]
    memtx_dir = arg[2]
end

box.cfg {
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    work_dir            = work_dir,
    memtx_dir           = memtx_dir,
}

require('console').listen(os.getenv('ADMIN'))
