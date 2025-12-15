local ffi = require('ffi')
local tap = require('tap')

local ffi_ccall = ffi.load('libfficcall')

local test = tap.test('ffi-ccall-arm64-fp-convention')
test:plan(3)

ffi.cdef[[
struct sz12_t {
	float f1;
	float f2;
	float f3;
};

/* Just return the value itself. */
struct sz12_t retsz12(struct sz12_t a);

/* Return sum of two. */
struct sz12_t sum2sz12(struct sz12_t a, struct sz12_t b);

/* Return sum of three. */
struct sz12_t sum3sz12(struct sz12_t a, struct sz12_t b, struct sz12_t c);
]]

-- For pretty error messages.
local sz12_mt = {__tostring = function(sz12)
  return string.format('{%d,%d,%d}', sz12.f1, sz12.f2, sz12.f3)
end}

local sz12_t = ffi.metatype('struct sz12_t', sz12_mt)

local function new_sz12(f1, f2, f3)
  return sz12_t(f1, f2, f3)
end

-- For pretty error messages.
local expected_mt = {__tostring = function(t)
  return '{'..table.concat(t, ',')..'}'
end}

local function assert_sz12(got, expected)
  assert(type(got) == 'cdata')
  assert(type(expected) == 'table')
  expected = setmetatable(expected, expected_mt)
  for i = 1, #expected do
    if got['f'..i] ~= expected[i] then
      error(('bad sz12_t value: got %s, expected %s'):format(
        tostring(got), tostring(expected)
      ))
    end
  end
  return true
end

-- Test arm64 calling convention for HFA structures.
-- Need structure which size is not multiple by 8.
local sz12_111 = new_sz12(1, 1, 1)
test:ok(assert_sz12(sz12_111, {1, 1, 1}), 'base')
local sz12_222 = ffi_ccall.sum2sz12(sz12_111, sz12_111)
test:ok(assert_sz12(sz12_222, {2, 2, 2}), '2 structures as args')
local sz12_333 = ffi_ccall.sum3sz12(sz12_111, sz12_111, sz12_111)
test:ok(assert_sz12(sz12_333, {3, 3, 3}), '3 structures as args')

test:done(true)
