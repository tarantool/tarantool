local tap = require('tap')
local test = tap.test('lj-366-strtab-correct-size'):skipcond({
  -- The test is ELF-specific, and because LuaJIT exports object
  -- files in ELF format for all operating systems except macOS
  -- and Windows we skip it on these OSes.
  -- See src/jit/bcsave.lua:bcsave_obj.
  ['Disabled on Windows'] = jit.os == 'Windows',
  ['Disabled on macOS'] = jit.os == 'OSX',
})

local ffi = require 'ffi'
local utils = require('utils')

-- Command below exports bytecode as an object file in ELF format:
-- $ luajit -b -n 'lango_team' -e 'print()' xxx.obj
-- $ file xxx.obj
-- xxx.obj: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV),
-- not stripped
--
-- With read_elf(1) it is possible to display entries in symbol
-- table section of the file, if it has one. Object file contains
-- a single symbol with name 'luaJIT_BC_lango_team':
--
-- $ readelf --symbols xxx.obj
--
-- luacheck: push no max_comment_line_length
--
-- Symbol table '.symtab' contains 2 entries:
--    Num:    Value          Size Type    Bind   Vis      Ndx Name
--      0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
--      1: 0000000000000000    66 OBJECT  GLOBAL DEFAULT    4 luaJIT_BC_lango_team
--
-- and to display the information contained in the file's section
-- headers, if it has any. For our purposes, we are interested in
-- .symtab section, so other sections are snipped in the output:
--
-- $ readelf --section-headers xxx.obj
-- There are 6 section headers, starting at offset 0x40:
--
-- Section Headers:
--   [Nr] Name              Type             Address           Offset
--          Size              EntSize          Flags  Link  Info  Align
-- ...
--
-- [ 3] .strtab           STRTAB           0000000000000000  00000223
--      0000000000000016  0000000000000000           0     0     1
-- ...
-- Reference numbers for strtab offset and size could be obtained
-- with readelf(1). Note that number system of these numbers is
-- hexadecimal.
--
-- luacheck: pop

-- Symbol name prefix for LuaJIT bytecode defined in bcsave.lua.
local LJBC_PREFIX = 'luaJIT_BC_'
local MODULE_NAME = 'lango_team'

local SYM_NAME_EXPECTED = LJBC_PREFIX .. MODULE_NAME
local EXPECTED_STRTAB_OFFSET = 0x223
-- The first \0 byte, which is index zero + length of the string
-- plus terminating \0 byte = 0x16.
local EXPECTED_STRTAB_SIZE =  1 + #SYM_NAME_EXPECTED + 1

-- Defined in elf.h.
local SHT_SYMTAB = 2

-- Using the same declarations as defined in <src/jit/bcsave.lua>.
ffi.cdef[[
typedef struct {
  uint8_t emagic[4], eclass, eendian, eversion, eosabi, eabiversion, epad[7];
  uint16_t type, machine;
  uint32_t version;
  uint32_t entry, phofs, shofs;
  uint32_t flags;
  uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstridx;
} ELF32header;

typedef struct {
  uint8_t emagic[4], eclass, eendian, eversion, eosabi, eabiversion, epad[7];
  uint16_t type, machine;
  uint32_t version;
  uint64_t entry, phofs, shofs;
  uint32_t flags;
  uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstridx;
} ELF64header;

typedef struct {
  uint32_t name, type, flags, addr, ofs, size, link, info, align, entsize;
} ELF32sectheader;

typedef struct {
  uint32_t name, type;
  uint64_t flags, addr, ofs, size;
  uint32_t link, info;
  uint64_t align, entsize;
} ELF64sectheader;

typedef struct {
  uint32_t name, value, size;
  uint8_t info, other;
  uint16_t sectidx;
} ELF32symbol;

typedef struct {
  uint32_t name;
  uint8_t info, other;
  uint16_t sectidx;
  uint64_t value, size;
} ELF64symbol;

typedef struct {
  ELF32header hdr;
  ELF32sectheader sect[6];
  ELF32symbol sym[2];
  uint8_t space[4096];
} ELF32obj;

typedef struct {
  ELF64header hdr;
  ELF64sectheader sect[6];
  ELF64symbol sym[2];
  uint8_t space[4096];
} ELF64obj;
]]

local is64_arch = {
  ['x64'] = true,
  ['arm64'] = true,
  ['arm64be'] = true,
  ['ppc'] = false,
  ['mips'] = false,
}

local is64 = is64_arch[jit.arch]

local function create_obj_file(name)
  local elf_filename = os.tmpname() .. '.obj'
  local lua_path = os.getenv('LUA_PATH')
  local lua_bin = utils.exec.luacmd(arg):match('%S+')
  local cmd_fmt = 'LUA_PATH="%s" %s -b -n "%s" -e "print()" %s'
  local cmd = (cmd_fmt):format(lua_path, lua_bin, name, elf_filename)
  local ret = os.execute(cmd)
  assert(ret == 0, 'create an object file')
  return elf_filename
end

-- Parses a buffer in an ELF format and returns an offset and a
-- size of strtab and symtab sections.
local function read_elf(elf_content)
  local ELFobj_type = ffi.typeof(is64 and 'ELF64obj *' or 'ELF32obj *')
  local ELFsectheader_type = ffi.typeof(is64 and 'ELF64sectheader *' or
                                        'ELF32sectheader *')
  local elf = ffi.cast(ELFobj_type, elf_content)
  local symtab_hdr, strtab_hdr
  -- Iterate by section headers.
  for i = 0, elf.hdr.shnum do
    local sec = ffi.cast(ELFsectheader_type, elf.sect[i])
    if sec.type == SHT_SYMTAB then
        symtab_hdr = sec
        strtab_hdr = ffi.cast(ELFsectheader_type, elf.sect[symtab_hdr.link])
        break
    end
  end

  assert(strtab_hdr ~= nil, 'section .strtab was not found')
  assert(symtab_hdr ~= nil, 'section .symtab was not found')

  return strtab_hdr, symtab_hdr
end

test:plan(3)

local elf_filename = create_obj_file(MODULE_NAME)
local elf_content = utils.tools.read_file(elf_filename)
assert(#elf_content ~= 0, 'cannot read an object file')

local strtab, symtab = read_elf(elf_content)
local strtab_size = tonumber(strtab.size)
local strtab_offset = tonumber(strtab.ofs)
local symtab_size = tonumber(symtab.size)
local sym_cnt = tonumber(symtab_size / symtab.entsize)
assert(sym_cnt ~= 0, 'number of symbols is zero')

test:is(strtab_size, EXPECTED_STRTAB_SIZE, 'check .strtab size')
test:is(strtab_offset, EXPECTED_STRTAB_OFFSET, 'check .strtab offset')

local strtab_str = string.sub(elf_content, strtab_offset,
                              strtab_offset + strtab_size)

local strtab_p = ffi.cast('char *', strtab_str)
local sym_is_found = false
for i = 1, sym_cnt do
  local sym_name = ffi.string(strtab_p + i)
  if SYM_NAME_EXPECTED == sym_name then
    sym_is_found = true
    break
  end
end

test:ok(sym_is_found == true, 'symbol is found')

local ret = os.remove(elf_filename)
assert(ret == true, 'cannot remove an object file')

test:done(true)
