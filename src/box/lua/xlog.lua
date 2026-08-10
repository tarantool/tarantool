---The `xlog` module provides the `pairs()` and `meta()` functions for
---reading Tarantool's snapshot files or write-ahead-log (WAL) files. A
---description of the file format is in the "Data persistence" section of
---the Tarantool documentation.
local xlog = {}

---@class xlog.entry
---@field HEADER xlog.entry_header The request header.
---@field BODY table The request body.

---@class xlog.entry_header
---@field type string The request type, e.g. 'INSERT', 'UPDATE',
---    'DELETE'.
---@field lsn integer The log sequence number.
---@field replica_id integer The identifier of the replica that
---    authored the request.
---@field timestamp number The request timestamp.

---@class xlog.meta
---@field filetype string The file type: 'SNAP' for a snapshot file,
---    'XLOG' for a write-ahead-log file.
---@field instance_uuid string The instance UUID.
---@field vclock table<integer, integer> The vclock of the file:
---    replica id to lsn.
---@field prev_vclock table<integer, integer> The previous vclock.

local internal = require('xlog.lib')
local fun = require('fun')

---Open a file, and allow iterating over one file entry at a time.
---
---Each result from `pairs()` is formatted with MsgPack so its structure
---can be specified with `__serialize`.
---
---**Example:**
---
---```lua
---xlog = require('xlog')
---t = {}
---for k, v in xlog.pairs('00000000000000000004.xlog') do
---    table.insert(t, setmetatable(v, { __serialize = 'map' }))
---end
---return t
---```
---
---The result will look like this:
---
---```
-----
---- - {'HEADER': {'lsn': 5, 'replica_id': 1, 'type': 'INSERT',
---                'timestamp': 1794563695.9269},
---    'BODY': {'space_id': 512, 'tuple': [2]}}
---...
---```
---
---Possible errors: the file does not contain properly formatted snapshot
---or write-ahead-log information.
---@param filename string The name of a snapshot or write-ahead-log file.
---@return fun(...): (integer, xlog.entry) iterator which can be used in a
---    for/end loop.
function xlog.pairs(filename)
    return fun.wrap(internal.pairs(filename))
end

---Open an xlog file and return its meta block.
---
---Possible errors: failed to open a file, or the file does not contain a
---properly formatted meta block.
---
---**Example:**
---
---```lua
---xlog.meta('00000000000000000000.snap')
---```
---@param filename string The name of a snapshot or write-ahead-log file.
---@return xlog.meta meta The meta block of the file.
function xlog.meta(filename)
    return internal.meta(filename)
end

return xlog
