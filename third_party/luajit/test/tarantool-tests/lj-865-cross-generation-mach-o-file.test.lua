local tap = require('tap')
local test = tap.test('lj-865-cross-generation-mach-o-file')
local utils = require('utils')
local ffi = require('ffi')

-- The test creates an object file in Mach-O format with LuaJIT
-- bytecode and checks the validity of the object file fields.
--
-- The original problem is reproduced with LuaJIT, which is built
-- with enabled AVX512F instructions. The support for AVX512F
-- could be checked in `/proc/cpuinfo` on Linux and
-- `sysctl hw.optional.avx512f` on Mac. AVX512F must be
-- implicitly enabled in a C compiler by passing a CPU codename.
-- Please take a look at the GCC Online Documentation [1] for
-- available CPU codenames. Also, see the Wikipedia for CPUs with
-- AVX-512 support [2].
-- Execute command below to detect the CPU codename:
-- `gcc -march=native -Q --help=target | grep march`.
--
-- 1. https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
-- 2. https://en.wikipedia.org/wiki/AVX-512#CPUs_with_AVX-512
--
-- Manual steps for reproducing are the following:
--
-- luacheck: push no max_comment_line_length
--
-- $ CC=gcc TARGET_CFLAGS='skylake-avx512' cmake -S . -B build
-- $ cmake --build build --parallel
-- $ echo > test.lua
-- $ LUA_PATH="src/?.lua;;" luajit -b -o osx -a arm test.lua test.o
-- $ file test.o
-- empty.o: DOS executable (block device driver)

-- The format of the Mach-O is described in the document
-- "OS X ABI Mach-O File Format Reference", published by Apple
-- company. The copy of the (now removed) official documentation
-- can be found here [1]. There is a good visual representation
-- of Mach-O format in "Mac OS X Internals" book (pages 67-68)
-- [2] and in the [3].
--
-- 0x0000000  ---------------------------------------
--             | 0xfeedface  MH_MAGIC                  magic
--             | ------------------------------------
--             | 0x00000012  CPU_TYPE_POWERPC          cputype
--             | ------------------------------------
-- struct      | 0x00000000  CPU_SUBTYPE_POWERPC_ALL   cpusubtype
-- mach_header | ------------------------------------
--             | 0x00000002  MH_EXECUTE                filetype
--             | ------------------------------------
--             | 0x0000000b  10 load commands          ncmds
--             | ------------------------------------
--             | 0x00000574  1396 bytes                sizeofcmds
--             | ------------------------------------
--             | 0x00000085  DYLDLINK TWOLEVEL         flags
--             --------------------------------------
--               Load commands
--             ---------------------------------------
--               Data
--             ---------------------------------------
--
-- 1. https://github.com/aidansteele/osx-abi-macho-file-format-reference
-- 2. https://reverseengineering.stackexchange.com/a/6357/46029
-- 3. http://formats.kaitai.io/mach_o/index.html
--
-- luacheck: pop

test:plan(1)

local CPU_SUBTYPE_ARM64 = 0x0
local CPU_TYPE_ARM64 = '0x100000c'
-- MH_MAGIC_64: mach-o, big-endian, x64.
local MH_MAGIC_64 = '0xfeedfacf'
-- Relocatable object file.
local MH_OBJECT = 0x1

-- The test creates an object file in Mach-O format with LuaJIT
-- bytecode and checks the validity of the object file fields.

-- Using the same declarations as defined in <src/jit/bcsave.lua>.
ffi.cdef[[
typedef struct
{
  uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags;
} mach_header;

typedef struct
{
  mach_header; uint32_t reserved;
} mach_header_64;

typedef struct {
  uint32_t cmd, cmdsize;
  char segname[16];
  uint64_t vmaddr, vmsize, fileoff, filesize;
  uint32_t maxprot, initprot, nsects, flags;
} mach_segment_command_64;

typedef struct {
  char sectname[16], segname[16];
  uint64_t addr, size;
  uint32_t offset, align, reloff, nreloc, flags;
  uint32_t reserved1, reserved2, reserved3;
} mach_section_64;

typedef struct {
  uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
} mach_symtab_command;

typedef struct {
  int32_t strx;
  uint8_t type, sect;
  uint16_t desc;
  uint64_t value;
} mach_nlist_64;

typedef struct {
  mach_header_64 hdr;
  mach_segment_command_64 seg;
  mach_section_64 sec;
  mach_symtab_command sym;
  mach_nlist_64 sym_entry;
  uint8_t space[4096];
} mach_obj_64;
]]

local function create_obj_file(name, arch)
  local mach_o_path = os.tmpname() .. '.o'
  local lua_path = os.getenv('LUA_PATH')
  local lua_bin = utils.exec.luacmd(arg):match('%S+')
  local cmd = ('LUA_PATH="%s" %s -b -n "%s" -o osx -a %s -e "print()" %s'):
              format(lua_path, lua_bin, name, arch, mach_o_path)
  local ret = os.execute(cmd)
  assert(ret == 0, 'cannot create an object file')
  return mach_o_path
end

-- Parses a buffer in the Mach-O format and returns its fields
-- in a table.
local function read_mach_o_hdr(buf, hw_arch)
  -- LuaJIT always generates 64-bit non-FAT Mach-O object files.
  assert(hw_arch == 'arm64')

  -- Mach-O object.
  local mach_obj_type = ffi.typeof('mach_obj_64 *')
  local obj = ffi.cast(mach_obj_type, buf)

  -- Mach-O object header.
  local mach_header = obj.hdr

  return mach_header
end

-- The function builds a Mach-O object file and validates its
-- header fields.
local function build_and_check_mach_o(subtest)
  local hw_arch = subtest.name
  -- LuaJIT always generates 64-bit, non-FAT Mach-O object files.
  assert(hw_arch == 'arm64')

  subtest:plan(5)

  local MODULE_NAME = 'lango_team'

  local mach_o_obj_path = create_obj_file(MODULE_NAME, hw_arch)
  local mach_o_buf = utils.tools.read_file(mach_o_obj_path)
  assert(mach_o_buf ~= nil and #mach_o_buf ~= 0, 'cannot read an object file')

  local mach_o = read_mach_o_hdr(mach_o_buf, hw_arch)

  -- Teardown.
  assert(os.remove(mach_o_obj_path), 'remove an object file')

  local magic_str = string.format('%#x', mach_o.magic)
  subtest:is(magic_str, MH_MAGIC_64, 'magic is correct in Mach-O')
  local cputype_str = string.format('%#x', mach_o.cputype)
  subtest:is(cputype_str, CPU_TYPE_ARM64, 'cputype is correct in Mach-O')
  subtest:is(mach_o.cpusubtype, CPU_SUBTYPE_ARM64,
             'cpusubtype is correct in Mach-O')
  subtest:is(mach_o.filetype, MH_OBJECT, 'filetype is correct in Mach-O')
  subtest:is(mach_o.ncmds, 2, 'ncmds is correct in Mach-O')
end

test:test('arm64', build_and_check_mach_o)

test:done(true)
