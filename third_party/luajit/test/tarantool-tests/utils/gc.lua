local M = {}

local ffi = require('ffi')

local LJ_GC_BLACK = 0x04
local LJ_STR_HASHLEN = 8
local GCref = ffi.abi('gc64') and 'uint64_t' or 'uint32_t'

ffi.cdef([[
  typedef struct {
]]..GCref..[[ nextgc;
    uint8_t   marked;
    uint8_t   gct;
    /* Need this fields for correct alignment and sizeof. */
    uint8_t   misc1;
    uint8_t   misc2;
  } GCHeader;
]])

function M.isblack(obj)
  local objtype = type(obj)
  local address = objtype == 'string'
    -- XXX: get strdata first and go back to GCHeader.
    and ffi.cast('char *', obj) - (ffi.sizeof('GCHeader') + LJ_STR_HASHLEN)
    -- XXX: FFI ABI forbids to cast functions objects
    -- to non-functional pointers, but we can get their address
    -- via tostring.
    or tonumber((tostring(obj):gsub(objtype .. ': ', '')))
  local marked = ffi.cast('GCHeader *', address).marked
  return bit.band(marked, LJ_GC_BLACK) == LJ_GC_BLACK
end

return M
