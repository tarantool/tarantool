local bit = require('bit')
local ffi = require('ffi')

ffi.cdef([[
    uint32_t box_dd_version_id(void);
    uint32_t box_latest_dd_version_id(void);
]])

local builtin = ffi.C

local mkversion_mt = {
    __tostring = function(self)
        return string.format('%s.%s.%s', self.major, self.minor, self.patch)
    end,
    __eq = function(lhs, rhs)
        return lhs.id == rhs.id
    end,
    __lt = function(lhs, rhs)
        return lhs.id < rhs.id
    end,
}

local function mkversion_new(major, minor, patch)
    local self = setmetatable({}, mkversion_mt)
    self.major = major
    self.minor = minor
    self.patch = patch
    self.id = bit.bor(bit.lshift(bit.bor(bit.lshift(major, 8), minor), 8),
                      patch)
    return self
end

-- Parse string with version in format 'A.B.C' to version object.
local function mkversion_from_string(version)
    local major, minor, patch = version:match('^(%d+)%.(%d+)%.(%d+)$')
    if major == nil then
        error('version should be in format A.B.C')
    end
    return mkversion_new(tonumber(major), tonumber(minor), tonumber(patch))
end

local function mkversion_from_id(version_id)
    local major = bit.band(bit.rshift(version_id, 16), 0xff)
    local minor = bit.band(bit.rshift(version_id, 8), 0xff)
    local patch = bit.band(version_id, 0xff)
    return mkversion_new(major, minor, patch)
end

-- Schema version of the snapshot.
local function mkversion_get()
    local version = builtin.box_dd_version_id()
    return mkversion_from_id(version)
end

-- Latest schema version this instance recognizes.
local function mkversion_get_latest()
    local version = builtin.box_latest_dd_version_id()
    return mkversion_from_id(version)
end

local function mkversion_from_tuple(tuple)
    local major, minor, patch = tuple:unpack(2, 4)
    patch = patch or 0
    if major and minor and type(major) == 'number' and
       type(minor) == 'number' and type(patch) == 'number' then
        return mkversion_new(major, minor, patch)
    end
    return nil
end

return setmetatable({
    new = mkversion_new,
    get = mkversion_get,
    get_latest = mkversion_get_latest,
    from_id = mkversion_from_id,
    from_tuple = mkversion_from_tuple,
    from_string = mkversion_from_string,
}, {
    __call = function(self, ...) return mkversion_new(...) end;
})
