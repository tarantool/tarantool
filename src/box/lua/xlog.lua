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
---@field server_id integer The identifier of the server that
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
---for k, v in xlog.pairs('00000000000000000000.xlog') do
---    table.insert(t, setmetatable(v, { __serialize = 'map' }))
---end
---return t
---```
---
---The first lines of the result will look like this:
---
---```
---(...)
------
---- - {'BODY':   {'space_id': 272, 'index_base': 1,
---                'key': ['max_id'], 'tuple': [['+', 2, 1]]},
---     'HEADER': {'type': 'UPDATE', 'timestamp': 1477846870.8541,
---                'lsn': 1, 'server_id': 1}}
---  - {'BODY':   {'space_id': 280,
---                 'tuple': [512, 1, 'tester', 'memtx', 0,
---                          {}, []]},
---     'HEADER': {'type': 'INSERT', 'timestamp': 1477846870.8597,
---                'lsn': 2, 'server_id': 1}}
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
