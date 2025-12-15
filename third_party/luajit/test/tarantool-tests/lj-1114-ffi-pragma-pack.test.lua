local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect parsing of `#pragma`
-- directive via FFI.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1114.

local test = tap.test('lj-1114-ffi-pragma-pack')
local ffi = require('ffi')

test:plan(2)

-- `cp->packstack` is the array of size `CPARSE_MAX_PACKSTACK`
-- (7). Before the patch, `cp->curpack` is checked to be less than
-- `CPARSE_MAX_PACKSTACK`, but then `cp->packstack` is accessed at
-- `cp->curpack + 1`, which is out of bounds, so `cp->curpack`
-- value is overwritten.
-- As a result, the incorrect pack value (1) is chosen after pop.
-- After the patch, the error is thrown in the case of overflow
-- (instead of rewriting the top pack slot value), so we use the
-- wrapper to catch the error.
local function ffi_cdef_wp()
  ffi.cdef[[
    #pragma pack(push, 1)
    #pragma pack(push, 1)
    #pragma pack(push, 1)
    #pragma pack(push, 1)
    #pragma pack(push, 8)
    #pragma pack(push, 8)
    #pragma pack(push, 8)
    #pragma pack(pop)
    struct aligned_struct {uint64_t a; uint8_t b;};
  ]]

  -- Got 9 in case of buffer overflow.
  return ffi.sizeof(ffi.new('struct aligned_struct'))
end

local err, msg = pcall(ffi_cdef_wp)

test:ok(not err, 'the error is thrown when counter overflows')
test:like(msg, 'chunk has too many syntax levels',
          'the error message is correct')

test:done(true)
