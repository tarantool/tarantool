local version = require('version')
local bit = require('bit')
local ffi = require('ffi')

ffi.cdef([[
    uint32_t box_dd_version_id(void);
    uint32_t box_latest_dd_version_id(void);
]])

local builtin = ffi.C

local function version_from_tuple(tuple)
    local major, minor, patch = tuple:unpack(2, 4)
    patch = patch or 0
    assert(type(major) == 'number')
    assert(type(minor) == 'number')
    return version.new(major, minor, patch)
end

local function version_from_id(version_id)
    local major = bit.band(bit.rshift(version_id, 16), 0xff)
    local minor = bit.band(bit.rshift(version_id, 8), 0xff)
    local patch = bit.band(version_id, 0xff)
    return version.new(major, minor, patch)
end

local function version_to_id(version)
    local id = bit.bor(bit.lshift(version.major, 8), version.minor)
    return bit.bor(bit.lshift(id, 8), version.patch)
end

local function dd_version()
    local version = builtin.box_dd_version_id()
    return version_from_id(version)
end

local function latest_dd_version()
    local version = builtin.box_latest_dd_version_id()
    return version_from_id(version)
end

box.internal.version_from_tuple = version_from_tuple
box.internal.dd_version = dd_version
box.internal.latest_dd_version = latest_dd_version
box.internal.version_to_id = version_to_id
